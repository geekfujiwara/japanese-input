#!/usr/bin/env python3
"""Signs the macOS input method for the Mac App Store and hands builds to TestFlight, through the
App Store Connect API (no extra packages: the JWT is signed with openssl).

The signing identities are made for one run and revoked afterwards, so CI needs nothing but an API key:

    asc_mac_release.py check   --bundle-id com.example.App          # API key, bundle ID, app record
    asc_mac_release.py prepare --bundle-id com.example.App --keychain K --keychain-password P --state S
    asc_mac_release.py testflight --state S --build 12 [--tester someone@example.com]
    asc_mac_release.py cleanup --state S                              # revoke what prepare made

Credentials come from the environment: ASC_KEY_ID, ASC_ISSUER_ID and ASC_KEY_PATH (the .p8). The key needs the
Admin role to create certificates and profiles.

`prepare` creates an Apple Distribution and a Mac Installer Distribution certificate from fresh keys, imports them
into the keychain, and creates and installs a Mac App Store provisioning profile. Apple re-signs what it distributes,
so revoking these certificates after the build is processed does not affect it. `testflight` waits for processing,
then gives the build to the internal testers (it never deletes a tester: a betaTester DELETE removes the person from
every app of the account).
"""
import argparse
import base64
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

API_ROOT = "https://api.appstoreconnect.apple.com"
# LibreSSL of macOS writes PKCS#12 files the keychain can import (OpenSSL 3 needs -legacy).
OPENSSL = "/usr/bin/openssl" if os.path.exists("/usr/bin/openssl") else "openssl"
WWDR_CERTIFICATES = [
    "https://www.apple.com/certificateauthority/AppleWWDRCAG3.cer",
    "https://www.apple.com/certificateauthority/AppleWWDRCAG4.cer",
    "https://www.apple.com/certificateauthority/AppleWWDRCAG6.cer",
]


# ---------------------------------------------------------------- API

def _b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def _der_to_raw(der: bytes) -> bytes:
    """openssl's DER signature (SEQUENCE{r, s}) to the raw r||s of JWS."""
    index = 2 if der[1] < 0x80 else 3 + (der[1] & 0x7F) - 1
    values = []
    for _ in range(2):
        length = der[index + 1]
        values.append(der[index + 2: index + 2 + length].lstrip(b"\x00").rjust(32, b"\x00"))
        index += 2 + length
    return values[0] + values[1]


def _credentials():
    key_id = os.environ.get("ASC_KEY_ID")
    issuer = os.environ.get("ASC_ISSUER_ID")
    path = os.path.expanduser(os.environ.get("ASC_KEY_PATH") or
                              f"~/.appstoreconnect/private_keys/AuthKey_{key_id}.p8")
    if not key_id or not issuer or not os.path.exists(path):
        sys.exit("Set ASC_KEY_ID, ASC_ISSUER_ID and ASC_KEY_PATH (the .p8 of an App Store Connect API key).")
    return key_id, issuer, path


_token = {"value": None, "expires": 0}


def token() -> str:
    if _token["value"] and time.time() < _token["expires"] - 60:
        return _token["value"]
    key_id, issuer, path = _credentials()
    now = int(time.time())
    header = {"alg": "ES256", "kid": key_id, "typ": "JWT"}
    payload = {"iss": issuer, "iat": now, "exp": now + 15 * 60, "aud": "appstoreconnect-v1"}
    signing_input = f"{_b64url(json.dumps(header).encode())}.{_b64url(json.dumps(payload).encode())}"
    der = subprocess.run([OPENSSL, "dgst", "-sha256", "-sign", path], input=signing_input.encode(),
                         capture_output=True, check=True).stdout
    _token["value"] = f"{signing_input}.{_b64url(_der_to_raw(der))}"
    _token["expires"] = now + 15 * 60
    return _token["value"]


class ApiError(Exception):
    def __init__(self, status, detail):
        super().__init__(f"HTTP {status}\n{detail}")
        self.status = status
        self.detail = detail


def request(method: str, path: str, body=None):
    url = path if path.startswith("http") else API_ROOT + path
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method.upper())
    req.add_header("Authorization", f"Bearer {token()}")
    if data:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req) as response:
            payload = response.read()
            return json.loads(payload) if payload else {}
    except urllib.error.HTTPError as error:
        raise ApiError(error.code, error.read().decode()) from None


def github_output(**values):
    """Hands values to later steps of a GitHub Actions job."""
    path = os.environ.get("GITHUB_OUTPUT")
    if path:
        with open(path, "a") as f:
            for key, value in values.items():
                f.write(f"{key}={value}\n")


# ---------------------------------------------------------------- bundle ID and app

def find_bundle_id(identifier: str):
    data = request("get", f"/v1/bundleIds?filter[identifier]={identifier}&fields[bundleIds]=identifier,platform")
    return next((b for b in data["data"] if b["attributes"]["identifier"] == identifier), None)


def ensure_bundle_id(identifier: str, name: str):
    bundle = find_bundle_id(identifier)
    if bundle:
        return bundle
    print(f"Registering the bundle ID {identifier} (macOS)...")
    return request("post", "/v1/bundleIds", {"data": {"type": "bundleIds", "attributes": {
        "identifier": identifier, "name": name, "platform": "MAC_OS"}}})["data"]


def find_app(identifier: str):
    data = request("get", f"/v1/apps?filter[bundleId]={identifier}&fields[apps]=name,bundleId")["data"]
    return next((a for a in data if a["attributes"]["bundleId"] == identifier), None)


def command_check(args):
    bundle = ensure_bundle_id(args.bundle_id, args.name)
    print(f"Bundle ID: {args.bundle_id} ({bundle['attributes']['platform']}, {bundle['id']})")
    app = find_app(args.bundle_id)
    if not app:
        sys.exit(
            f"No App Store Connect app uses {args.bundle_id} yet. The API cannot create one: in App Store Connect\n"
            f"choose Apps > + > New App, platform macOS, bundle ID {args.bundle_id}, SKU of your choice, then run again.")
    print(f"App: {app['attributes']['name']} ({app['id']})")
    github_output(app_id=app["id"])
    return app


# ---------------------------------------------------------------- signing

def run(*command, **kwargs):
    return subprocess.run(list(command), check=True, **kwargs)


def create_certificate(kind: str, workdir: str, keychain: str, password: str) -> dict:
    """A certificate of `kind` from a fresh key, imported into `keychain`. Returns its id, SHA-1 and team."""
    key = os.path.join(workdir, f"{kind}.key")
    csr = os.path.join(workdir, f"{kind}.csr")
    run(OPENSSL, "req", "-new", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", csr,
        "-subj", f"/CN=Astelio CI {kind}/C=JP", stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with open(csr) as f:
        csr_content = f.read()
    try:
        certificate = request("post", "/v1/certificates", {"data": {"type": "certificates", "attributes": {
            "certificateType": kind, "csrContent": csr_content}}})["data"]
    except ApiError as error:
        if error.status == 409:
            sys.exit(f"Could not create a {kind} certificate; the team may have reached the limit.\n"
                     f"Revoke an unused one in Certificates, Identifiers & Profiles, then run again.\n{error.detail}")
        raise
    der = os.path.join(workdir, f"{kind}.cer")
    pem = os.path.join(workdir, f"{kind}.pem")
    with open(der, "wb") as f:
        f.write(base64.b64decode(certificate["attributes"]["certificateContent"]))
    run(OPENSSL, "x509", "-inform", "DER", "-in", der, "-out", pem)
    fingerprint = subprocess.run([OPENSSL, "x509", "-in", pem, "-noout", "-fingerprint", "-sha1"],
                                 capture_output=True, text=True, check=True).stdout
    sha1 = fingerprint.split("=", 1)[1].strip().replace(":", "")
    subject = subprocess.run([OPENSSL, "x509", "-in", pem, "-noout", "-subject"],
                             capture_output=True, text=True, check=True).stdout
    team = re.search(r"OU\s*=\s*([A-Z0-9]{10})", subject)
    p12 = os.path.join(workdir, f"{kind}.p12")
    run(OPENSSL, "pkcs12", "-export", "-inkey", key, "-in", pem, "-out", p12, "-passout", f"pass:{password}")
    run("security", "import", p12, "-k", keychain, "-P", password, "-T", "/usr/bin/codesign",
        "-T", "/usr/bin/productbuild", "-T", "/usr/bin/security", stdout=subprocess.DEVNULL)
    os.remove(key)
    print(f"Created {kind} certificate {certificate['id']} ({certificate['attributes'].get('name')}), SHA-1 {sha1}")
    return {"id": certificate["id"], "sha1": sha1, "team": team.group(1) if team else None}


def install_intermediates(workdir: str, keychain: str):
    """codesign needs the Apple WWDR intermediates to build the chain on a clean machine."""
    for url in WWDR_CERTIFICATES:
        path = os.path.join(workdir, os.path.basename(url))
        try:
            with urllib.request.urlopen(url) as response, open(path, "wb") as f:
                f.write(response.read())
            subprocess.run(["security", "import", path, "-k", keychain], capture_output=True)
        except urllib.error.URLError as error:
            print(f"warning: could not fetch {url}: {error}")


def install_profile(profile: dict) -> str:
    content = base64.b64decode(profile["attributes"]["profileContent"])
    uuid = profile["attributes"]["uuid"]
    for folder in ("~/Library/MobileDevice/Provisioning Profiles",
                   "~/Library/Developer/Xcode/UserData/Provisioning Profiles"):
        folder = os.path.expanduser(folder)
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, f"{uuid}.provisionprofile"), "wb") as f:
            f.write(content)
    return uuid


def save_state(path: str, state: dict):
    with open(path, "w") as f:
        json.dump(state, f, indent=2)


def command_prepare(args):
    app = command_check(args)
    bundle = find_bundle_id(args.bundle_id)
    state = {"app_id": app["id"], "bundle_id": args.bundle_id, "certificates": [], "profile_id": None}
    workdir = tempfile.mkdtemp(prefix="asc-signing-")
    try:
        install_intermediates(workdir, args.keychain)
        application = create_certificate("DISTRIBUTION", workdir, args.keychain, args.keychain_password)
        state["certificates"].append(application["id"])
        save_state(args.state, state)
        installer = create_certificate("MAC_INSTALLER_DISTRIBUTION", workdir, args.keychain, args.keychain_password)
        state["certificates"].append(installer["id"])
        save_state(args.state, state)
        run("security", "set-key-partition-list", "-S", "apple-tool:,apple:,codesign:", "-s",
            "-k", args.keychain_password, args.keychain, stdout=subprocess.DEVNULL)

        name = f"{args.name} Mac App Store CI {int(time.time())}"
        profile = request("post", "/v1/profiles", {"data": {
            "type": "profiles",
            "attributes": {"name": name, "profileType": "MAC_APP_STORE"},
            "relationships": {
                "bundleId": {"data": {"type": "bundleIds", "id": bundle["id"]}},
                "certificates": {"data": [{"type": "certificates", "id": application["id"]}]},
            }}})["data"]
        state["profile_id"] = profile["id"]
        uuid = install_profile(profile)
        state.update(team_id=application["team"], profile_name=name, profile_uuid=uuid,
                     application_sha1=application["sha1"], installer_sha1=installer["sha1"])
        save_state(args.state, state)
        print(f"Installed the profile \"{name}\" ({uuid}); team {application['team']}")
        github_output(team_id=application["team"], profile_name=name, application_sha1=application["sha1"],
                      installer_sha1=installer["sha1"])
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def command_cleanup(args):
    if not os.path.exists(args.state):
        print("Nothing to clean up.")
        return
    with open(args.state) as f:
        state = json.load(f)
    if state.get("profile_id"):
        try:
            request("delete", f"/v1/profiles/{state['profile_id']}")
            print(f"Deleted the profile {state['profile_id']}")
        except ApiError as error:
            print(f"warning: could not delete the profile: {error}")
    for certificate in state.get("certificates", []):
        try:
            request("delete", f"/v1/certificates/{certificate}")
            print(f"Revoked the certificate {certificate}")
        except ApiError as error:
            print(f"warning: could not revoke the certificate {certificate}: {error}")


# ---------------------------------------------------------------- TestFlight

def find_build(app_id: str, number: str):
    data = request("get", f"/v1/builds?filter[app]={app_id}&filter[version]={number}"
                          "&filter[preReleaseVersion.platform]=MAC_OS&limit=1"
                          "&fields[builds]=version,processingState")["data"]
    return data[0] if data else None


def wait_for_build(app_id: str, number: str, minutes: int):
    deadline = time.time() + minutes * 60
    while time.time() < deadline:
        build = find_build(app_id, number)
        state = build["attributes"]["processingState"] if build else "not arrived"
        print(f"  build {number}: {state}", flush=True)
        if build and state == "VALID":
            return build
        if state in {"FAILED", "INVALID"}:
            sys.exit(f"App Store Connect could not process build {number}: {state}. Its email says why.")
        time.sleep(30)
    sys.exit(f"Build {number} was not processed within {minutes} minutes; run `testflight` again later.")


def ensure_internal_group(app_id: str):
    groups = request("get", f"/v1/apps/{app_id}/betaGroups?fields[betaGroups]=name,isInternalGroup")["data"]
    internal = next((g for g in groups if g["attributes"]["isInternalGroup"]), None)
    if internal:
        return internal
    print("Creating the internal testing group...")
    return request("post", "/v1/betaGroups", {"data": {
        "type": "betaGroups", "attributes": {"name": "Internal Testers", "isInternalGroup": True},
        "relationships": {"app": {"data": {"type": "apps", "id": app_id}}}}})["data"]


def ensure_beta_localization(app_id: str, feedback_email: str):
    """Without one, adding a tester fails with 409 STATE_ERROR."""
    if request("get", f"/v1/apps/{app_id}/betaAppLocalizations")["data"]:
        return
    locale = request("get", f"/v1/apps/{app_id}?fields[apps]=primaryLocale")["data"]["attributes"].get(
        "primaryLocale") or "ja"
    request("post", "/v1/betaAppLocalizations", {"data": {
        "type": "betaAppLocalizations", "attributes": {"locale": locale, "feedbackEmail": feedback_email},
        "relationships": {"app": {"data": {"type": "apps", "id": app_id}}}}})


def command_testflight(args):
    with open(args.state) as f:
        state = json.load(f)
    app_id = state["app_id"]
    build = wait_for_build(app_id, args.build, args.wait_minutes)
    group = ensure_internal_group(app_id)
    request("post", f"/v1/betaGroups/{group['id']}/relationships/builds",
            {"data": [{"type": "builds", "id": build["id"]}]})
    print(f"Build {args.build} is in \"{group['attributes']['name']}\".")
    if args.tester:
        testers = request("get", f"/v1/betaGroups/{group['id']}/betaTesters?fields[betaTesters]=email&limit=200")
        if any(t["attributes"]["email"].lower() == args.tester.lower() for t in testers["data"]):
            print(f"{args.tester} is already a tester; TestFlight shows the build as an update.")
        else:
            ensure_beta_localization(app_id, args.tester)
            # One request with the group: never delete or reuse tester records (they are per app).
            request("post", "/v1/betaTesters", {"data": {
                "type": "betaTesters", "attributes": {"email": args.tester},
                "relationships": {"betaGroups": {"data": [{"type": "betaGroups", "id": group["id"]}]}}}})
            print(f"Invited {args.tester} (an App Store Connect user of the team) to internal testing.")
    print("Open TestFlight on the Mac to install it.")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("check", "prepare"):
        p = sub.add_parser(name)
        p.add_argument("--bundle-id", required=True)
        p.add_argument("--name", default="Astelio IME")
        if name == "prepare":
            p.add_argument("--keychain", required=True)
            p.add_argument("--keychain-password", required=True)
            p.add_argument("--state", required=True)
    p = sub.add_parser("testflight")
    p.add_argument("--state", required=True)
    p.add_argument("--build", required=True, help="CFBundleVersion of the uploaded build")
    p.add_argument("--tester", help="email of an App Store Connect user to invite to internal testing")
    p.add_argument("--wait-minutes", type=int, default=45)
    p = sub.add_parser("cleanup")
    p.add_argument("--state", required=True)
    args = parser.parse_args()
    {"check": command_check, "prepare": command_prepare, "testflight": command_testflight,
     "cleanup": command_cleanup}[args.command](args)


if __name__ == "__main__":
    main()

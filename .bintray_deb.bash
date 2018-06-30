#! /bin/bash

REPO_TYPE=debian
PACKAGE_VERSION=$1  # e.g., 0.2.1-234.master.abcdefa
PACKAGE_NAME=$2     # e.g., ponyc
DISTRO=$3

if [[ "$PACKAGE_VERSION" == "" ]]; then
  echo "Error! PACKAGE_VERSION (argument 1) required!"
  exit 1
fi

if [[ "$PACKAGE_NAME" == "" ]]; then
  echo "Error! PACKAGE_NAME (argument 2) required!"
  exit 1
fi

if [[ "$DISTRO" == "" ]]; then
  echo "Error! DISTRO (argument 3) required!"
  exit 1
fi

# TODO: cut "ponyc" out of the repo names
BINTRAY_REPO_NAME="ponylang-test"
OUTPUT_TARGET="bintray_${REPO_TYPE}_${DISTRO}.json"

DATE="$(date +%Y-%m-%d)"

case "$REPO_TYPE" in
  "debian")
    FILES="\"files\":
        [
          {
            \"includePattern\": \"/home/travis/build/dipinhora/ponyc/(ponyc_.*${DISTRO}.*.deb)\", \"uploadPattern\": \"pool/main/p/ponyc/\$1\",
            \"matrixParams\": {
            \"deb_distribution\": \"${DISTRO}\",
            \"deb_component\": \"ponylang\",
            \"deb_architecture\": \"amd64\"}
         }
       ],
       \"publish\": true" 
    ;;
esac

JSON="{
  \"package\": {
    \"repo\": \"$BINTRAY_REPO_NAME\",
    \"name\": \"$PACKAGE_NAME\",
    \"subject\": \"dipin-ponylang-test\",
    \"website_url\": \"https://www.ponylang.org/\",
    \"issue_tracker_url\": \"https://github.com/ponylang/ponyc/issues\",
    \"vcs_url\": \"https://github.com/ponylang/ponyc.git\"
  },
  \"version\": {
    \"name\": \"$PACKAGE_VERSION\",
    \"desc\": \"ponyc release $PACKAGE_VERSION\",
    \"released\": \"$DATE\",
    \"vcs_tag\": \"$PACKAGE_VERSION\",
    \"gpgSign\": false
  },"

JSON="$JSON$FILES}"

echo "Writing JSON to file: $OUTPUT_TARGET, from within $(pwd) ..."
echo "$JSON" > "$OUTPUT_TARGET"

echo "=== WRITTEN FILE =========================="
cat -v "$OUTPUT_TARGET"
echo "==========================================="


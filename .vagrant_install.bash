#!/bin/bash

# from https://github.com/travis-ci/travis-build/blob/master/lib/travis/build/templates/header.sh#L243-L260
ANSI_RED="\\033[31;1m"
ANSI_RESET="\\033[0m"

travis_retry() {
  local result=0
  local count=1
  while [ $count -le 3 ]; do
    [ $result -ne 0 ] && {
      echo -e "\\n${ANSI_RED}The command \"$*\" failed. Retrying, $count of 3.${ANSI_RESET}\\n" >&2
    }
    "$@" && { result=0 && break; } || result=$?
    count=$((count + 1))
    sleep 1
  done
  [ $count -gt 3 ] && {
    echo -e "\\n${ANSI_RED}The command \"$*\" failed 3 times.${ANSI_RESET}\\n" >&2
  }
  return $result
}

download_vagrant(){
  echo "Downloading and installing vagrant/libvirt..."
  travis_retry apt_update_sources
  travis_retry sudo apt-get install -y libvirt-bin libvirt-dev qemu-utils qemu
  sudo libvirtd --version
  sudo /etc/init.d/libvirt-bin restart
  travis_retry wget "https://releases.hashicorp.com/vagrant/2.0.1/vagrant_2.0.1_x86_64.deb"
  sudo dpkg -i vagrant_2.0.1_x86_64.deb
  rm vagrant_2.0.1_x86_64.deb
  travis_retry vagrant plugin install vagrant-libvirt --plugin-version 0.0.35
  cd .ci-vagrantfiles/${VAGRANT_ENV}
  travis_retry sudo vagrant up --provider=libvirt
}

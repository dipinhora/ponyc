#!/bin/bash

set -o errexit
set -o nounset
set -vx

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

apt_update_sources(){
  # based on https://unix.stackexchange.com/a/175147
  if ! { sudo apt-get update 2>&1 || echo E: update failed; } | tee apt-get-update-output | grep -Eq '(^W: Failed|^E:)'; then
    cat apt-get-update-output
    return 0
  else
    cat apt-get-update-output
    return 1
  fi
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
  travis_retry sudo vagrant up --provider=libvirt
}

download_compiler(){
  echo "Downloading and installing the compiler..."

#  travis_retry sudo add-apt-repository -m ppa:ubuntu-toolchain-r/test -y
  echo "deb http://ppa.launchpad.net/ubuntu-toolchain-r/test/ubuntu xenial main " | sudo tee -a /etc/apt/sources.list
  echo "deb-src http://ppa.launchpad.net/ubuntu-toolchain-r/test/ubuntu xenial main " | sudo tee -a /etc/apt/sources.list
  travis_retry sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 60C317803A41BA51845E371A1E9377A2BA9EF27F
  travis_retry apt_update_sources
  travis_retry sudo apt-get install -y "${ICC1}" "${ICXX1}" libssl-dev
}

download_llvm(){
  echo "Downloading and installing the LLVM specified by envvars..."

#  echo "deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty main" | sudo tee -a /etc/apt/sources.list
#  echo "deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty main" | sudo tee -a /etc/apt/sources.list
  echo "deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial main" | sudo tee -a /etc/apt/sources.list
  echo "deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenail main" | sudo tee -a /etc/apt/sources.list
  travis_retry wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key|sudo apt-key add -
  travis_retry apt_update_sources
  travis_retry sudo apt-get install -y llvm-3.9
}

download_libunwind(){
  echo "Downloading and building libunwind..."

  travis_retry wget "http://download.savannah.nongnu.org/releases/libunwind/libunwind-1.2.1.tar.gz"
  tar -xzf libunwind-1.2.1.tar.gz
  pushd libunwind-1.2.1 && ./configure --prefix=/usr && make -j2 && sudo make install
  popd
  pushd libunwind-1.2.1 && ./configure --prefix=/usr/local && make -j2 && sudo make install
  popd
}

download_pcre(){
  echo "Downloading and building PCRE2..."

  travis_retry wget "ftp://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre2-10.21.tar.bz2"
  tar -xjf pcre2-10.21.tar.bz2
  pushd pcre2-10.21 && ./configure --prefix=/usr && make -j2 && sudo make install
  popd
}

set_linux_compiler(){
  echo "Setting $ICC1 and $ICXX1 as default compiler"

  sudo update-alternatives --install /usr/bin/gcc gcc "/usr/bin/$ICC1" 60 --slave /usr/bin/g++ g++ "/usr/bin/$ICXX1"
}

echo "Installing ponyc build dependencies..."

case "${VAGRANT_ENV}" in

  "ubuntu-i686")
    download_vagrant
#    sudo vagrant ssh -c "cp -r /vagrant ~/"
#    sudo vagrant ssh -c "ls -laF"
    cat /proc/cpuinfo
    sudo vagrant ssh -c "cd /vagrant && ls -laF"
    sudo vagrant ssh -c "cat /proc/cpuinfo"
    sudo vagrant ssh -c "dmesg"
    sudo vagrant ssh -c "cd /vagrant && env VAGRANT_ENV=${VAGRANT_ENV}-install ICC1=${ICC1} ICXX1=${ICXX1} CC1=${CC1} CXX1=${CXX1} bash .vagrant_install.bash"
    sudo vagrant ssh -c "cd /vagrant && make CC=\"$CC1\" CXX=\"$CXX1\" config=debug verbose=1 -j2 all"
    sudo vagrant ssh -c "cd /vagrant && make CC=\"$CC1\" CXX=\"$CXX1\" config=debug verbose=1 test-ci"
    sudo vagrant ssh -c "cd /vagrant && make CC=\"$CC1\" CXX=\"$CXX1\" config=release verbose=1 -j2 all"
    sudo vagrant ssh -c "cd /vagrant && make CC=\"$CC1\" CXX=\"$CXX1\" config=release verbose=1 test-ci"
  ;;

  "ubuntu-i686-install")
    download_compiler
    download_llvm
    download_pcre
    download_libressl
    download_libunwind
    set_linux_compiler
  ;;

  "freebsd-x86_64")
    date
    download_vagrant
    date
    sudo vagrant ssh -c "cd /vagrant && env VAGRANT_ENV=${VAGRANT_ENV}-install bash .vagrant_install.bash"
    date
    sudo vagrant ssh -c "cd /vagrant && gmake config=debug -j2 all"
    sudo vagrant ssh -c "cd /vagrant && gmake config=debug test-ci"
    date
    sudo vagrant ssh -c "cd /vagrant && gmake config=release -j2 all"
    sudo vagrant ssh -c "cd /vagrant && gmake config=release test-ci"
    date
  ;;

  "freebsd-x86_64-install")
    travis_retry sudo pkg update
    travis_retry sudo pkg install -y gmake
    travis_retry sudo pkg install -y llvm39
    travis_retry sudo pkg install -y pcre2
    travis_retry sudo pkg install -y libunwind
  ;;

  *)
    echo "ERROR: An unrecognized vagrant environment was found! VAGRANT_ENV: ${VAGRANT_ENV}"
    exit 1
  ;;

esac

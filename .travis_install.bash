#! /bin/bash

set -o errexit
set -o nounset

if [[ "$TRAVIS_BRANCH" == "release" && "$FAVORITE_CONFIG" != "yes" ]]
then
  echo "This is a release branch and there's nothing this matrix element must do."
  exit 0
fi

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

download_compiler(){
  echo "Downloading and installing the compiler..."

  travis_retry sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
  travis_retry sudo apt-get -qq update
  travis_retry sudo apt-get install -y "${ICC1}" "${ICXX1}"

  case "${CROSS_ARCH}" in

    "i686")
      echo "Installing ponyc cross build dependencies..."
      sudo dpkg --add-architecture i386
      travis_retry sudo apt-get -qq update
      travis_retry sudo apt-get install libc6:i386 libc6-dev:i386 linux-libc-dev:i386 zlib1g:i386 zlib1g-dev:i386
      travis_retry sudo apt-get install "${ICXX1}" "${ICXX1}-multilib" "${ICC1}-multilib"
    ;;

    "armv7-a")
      echo "Installing ponyc cross build dependencies..."
      pushd /tmp
      travis_retry wget "https://releases.linaro.org/components/toolchain/binaries/6.4-2017.11/arm-linux-gnueabihf/gcc-linaro-6.4.1-2017.11-x86_64_arm-linux-gnueabihf.tar.xz"
      sudo tar xJf gcc-linaro-6.4.1-2017.11-x86_64_arm-linux-gnueabihf.tar.xz -C /usr/local --strip 1
      arm-linux-gnueabihf-gcc --version
      popd
      travis_retry sudo wget https://github.com/multiarch/qemu-user-static/releases/download/v2.9.1-1/qemu-arm-static -O /usr/bin/qemu-arm-static
      sudo chmod +x /usr/bin/qemu-arm-static
    ;;

  esac

}

download_llvm(){
  echo "Downloading and installing the LLVM specified by envvars..."

  travis_retry wget "http://llvm.org/releases/${LLVM_VERSION}/clang+llvm-${LLVM_VERSION}-x86_64-linux-gnu-debian8.tar.xz"
  tar -xf clang+llvm*
  pushd clang+llvm* && sudo mkdir /tmp/llvm && sudo cp -r ./* /tmp/llvm/
  sudo ln -s "/tmp/llvm/bin/llvm-config" "/usr/local/bin/${LLVM_CONFIG}"
  popd
}

download_libunwind(){
  echo "Downloading and building libunwind..."

  travis_retry wget "http://download.savannah.nongnu.org/releases/libunwind/libunwind-1.2.1.tar.gz"
  tar -xzf libunwind-1.2.1.tar.gz
  pushd libunwind-1.2.1 && ./configure --prefix=/usr && make && sudo make install
  popd
  pushd libunwind-1.2.1 && ./configure --prefix=/usr/local && make && sudo make install
  popd
  if [[ "${CROSS_ARCH}" != "" ]]
  then
    echo "Cross building libunwind..."
    pushd libunwind-1.2.1 && ./configure --prefix=/usr/cross --host="${CROSS_TRIPLE}" CC="${CROSS_CC}" CXX="${CROSS_CXX}" CFLAGS="${CROSS_CFLAGS}" LDFLAGS="${CROSS_LDFLAGS}" && make && sudo make install
    popd
  fi
}

download_pcre(){
  echo "Downloading and building PCRE2..."

  travis_retry wget "ftp://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre2-10.21.tar.bz2"
  tar -xjf pcre2-10.21.tar.bz2
  pushd pcre2-10.21 && ./configure --prefix=/usr && make && sudo make install
  popd
  if [[ "${CROSS_ARCH}" != "" ]]
  then
    echo "Cross building PCRE2..."
    pushd pcre2-10.21 && ./configure --prefix=/usr/cross --host="${CROSS_TRIPLE}" CC="${CROSS_CC}" CXX="${CROSS_CXX}" CFLAGS="${CROSS_CFLAGS}" LDFLAGS="${CROSS_LDFLAGS}" && make && sudo make install
    popd
    echo "Downloading and cross building libressl..."
    travis_retry wget "https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-2.4.5.tar.gz"
    tar -xzf libressl-2.4.5.tar.gz
    pushd libressl-2.4.5 && ./configure --prefix=/usr/cross --disable-asm --host="${CROSS_TRIPLE}" CC="${CROSS_CC}" CXX="${CROSS_CXX}" CFLAGS="${CROSS_CFLAGS}" LDFLAGS="${CROSS_LDFLAGS}" && make && sudo make install
    popd
  fi
}

download_libressl(){
  if [[ "${CROSS_ARCH}" != "" ]]
  then
    echo "Downloading and cross building libressl..."
    travis_retry wget "https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-2.4.5.tar.gz"
    tar -xzf libressl-2.4.5.tar.gz
    pushd libressl-2.4.5 && ./configure --prefix=/usr/cross --disable-asm --host="${CROSS_TRIPLE}" CC="${CROSS_CC}" CXX="${CROSS_CXX}" CFLAGS="${CROSS_CFLAGS}" LDFLAGS="${CROSS_LDFLAGS}" && make && sudo make install
    popd
  fi
}

set_linux_compiler(){
  echo "Setting $ICC1 and $ICXX1 as default compiler"

  sudo update-alternatives --install /usr/bin/gcc gcc "/usr/bin/$ICC1" 60 --slave /usr/bin/g++ g++ "/usr/bin/$ICXX1"
}

echo "Installing ponyc build dependencies..."

case "${TRAVIS_OS_NAME}:${LLVM_CONFIG}" in

  "linux:llvm-config-3.7")
    download_compiler
    download_llvm
    download_pcre
    download_libressl
    download_libunwind
    set_linux_compiler
  ;;

  "linux:llvm-config-3.8")
    download_compiler
    download_llvm
    download_pcre
    download_libressl
    download_libunwind
    set_linux_compiler
  ;;

  "linux:llvm-config-3.9")
    download_compiler
    download_llvm
    download_pcre
    download_libressl
    download_libunwind
    set_linux_compiler
  ;;

  "linux:llvm-config-4.0")
    download_compiler
    download_llvm
    download_pcre
    download_libressl
    download_libunwind
    set_linux_compiler
  ;;

  "linux:llvm-config-5.0")
    download_compiler
    download_llvm
    download_pcre
    download_libressl
    download_libunwind
    set_linux_compiler
  ;;

  "osx:llvm-config-3.7")
    brew update
    brew install pcre2
    brew install libressl

    brew install llvm37
  ;;

  "osx:llvm-config-3.8")
    brew update
    brew install pcre2
    brew install libressl

    brew install llvm38
  ;;

  "osx:llvm-config-3.9")
    brew update
    brew install shellcheck
    shellcheck ./.*.bash ./*.bash

    brew install pcre2
    brew install libressl

    brew install llvm@3.9
    brew link --overwrite --force llvm@3.9
    mkdir llvmsym
    ln -s "$(which llvm-config)" llvmsym/llvm-config-3.9
    ln -s "$(which clang++)" llvmsym/clang++-3.9

    # do this elsewhere:
    #export PATH=llvmsym/:$PATH
  ;;

  "osx:llvm-config-4.0")
    brew update
    brew install shellcheck
    shellcheck ./.*.bash ./*.bash

    brew install pcre2
    brew install libressl

    brew install llvm@4
    brew link --overwrite --force llvm@4
    mkdir llvmsym
    ln -s "$(which llvm-config)" llvmsym/llvm-config-4.0
    ln -s "$(which clang++)" llvmsym/clang++-4.0

    # do this elsewhere:
    export PATH=llvmsym/:$PATH
  ;;

  "osx:llvm-config-5.0")
    brew update
    brew install shellcheck
    shellcheck ./.*.bash ./*.bash

    brew install pcre2
    brew install libressl

    brew install llvm@5
    brew link --overwrite --force llvm@5
    mkdir llvmsym
    ln -s "$(which llvm-config)" llvmsym/llvm-config-5.0
    ln -s "$(which clang++)" llvmsym/clang++-5.0

    # do this elsewhere:
    export PATH=llvmsym/:$PATH
  ;;

  *)
    echo "ERROR: An unrecognized OS and LLVM tuple was found! Consider OS: ${TRAVIS_OS_NAME} and LLVM: ${LLVM_CONFIG}"
    exit 1
  ;;

esac

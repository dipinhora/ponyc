#! /bin/bash

set -o errexit
set -o nounset

ponyc-test(){
  echo "Building and testing ponyc..."
  make CC="$CC1" CXX="$CXX1" test-ci
}

build_and_submit_deb_src(){
  deb_distro=$1
  rm -f debian/changelog
  dch --package ponyc -v ${package_version}-1ubuntu1~${deb_distro}1 -D ${deb_distro} --controlmaint --create "Release ${package_version}"
  if [[ "$deb_distro" == "trusty" ]]
  then
    EDITOR=/bin/true dpkg-source --commit . removepcredep
  fi
  debuild -S
  dput custom-ppa ../ponyc_${package_version}-1ubuntu1~${deb_distro}1_source.changes
}

ponyc-build-packages(){
  package_version=$(cat VERSION)

#  echo "Installing ruby, rpm, and fpm..."
#  rvm use 2.2.3 --default
#  sudo apt-get install -y rpm
#  gem install fpm

#  echo "Building ponyc packages for deployment..."
#  make CC="$CC1" CXX="$CXX1" verbose=1 arch=x86-64 tune=intel package_name="ponyc" package_base_version="${package_version}" deploy

  echo "Install debuild, dch, dput..."
  sudo apt-get install -y devscripts build-essential lintian debhelper python-paramiko

  echo "Decrypting and Importing gpg keys..."
  openssl aes-256-cbc -K $encrypted_0f44361077f1_key -iv $encrypted_0f44361077f1_iv -in gpg-files.tar.enc -out gpg-files.tar -d
  tar -xvf gpg-files.tar
  gpg --import dipin-secret-gpg.key
  gpg --import-ownertrust dipin-ownertrust-gpg.txt

  echo "Kicking off ponyc packaging for PPA..."
  wget https://github.com/dipinhora/ponyc/archive/${package_version}.tar.gz -O ponyc_${package_version}.orig.tar.gz
  tar -xvzf ponyc_${package_version}.orig.tar.gz
  cd ponyc-${package_version}
  cp -r .packaging/deb debian
  cp LICENSE debian/copyright

  echo "[custom-ppa]" >> ~/.dput.cf
  echo "fqdn = ppa.launchpad.net" >> ~/.dput.cf
  echo "method = sftp" >> ~/.dput.cf
  echo "incoming = ~dipinhora/ubuntu/testppa/" >> ~/.dput.cf
  echo "login = dipinhora" >> ~/.dput.cf
  echo "allow_unsigned_uploads = 0" >> ~/.dput.cf

  mkdir -p ~/.ssh
  echo "Host ppa.launchpad.net" >> ~/.ssh/config
  echo "    StrictHostKeyChecking no" >> ~/.ssh/config
  echo "    IdentityFile sshkey" >> ~/.ssh/config
  sudo chmod 400 ~/.ssh/config

  openssl aes-256-cbc -K $encrypted_0f44361077f1_key -iv $encrypted_0f44361077f1_iv -in sshkey.enc -out sshkey -d
  sudo chmod 600 sshkey

  build_and_submit_deb_src xenial
  build_and_submit_deb_src artful
  build_and_submit_deb_src bionic
  build_and_submit_deb_src cosmic

  # run trusty last because we will modify things to not rely on pcre2
  # remove pcre dependency from package and tests
  sed -i 's/, libpcre2-dev//g' debian/control
  sed -i 's#use glob#//use glob#g' packages/stdlib/_test.pony
  sed -i 's#glob.Main.make#None//glob.Main.make#g' packages/stdlib/_test.pony
  sed -i 's#use regex#//use regex#g' packages/stdlib/_test.pony
  sed -i 's#regex.Main.make#//regex.Main.make#g' packages/stdlib/_test.pony
  build_and_submit_deb_src trusty

  # COPR for fedora/centos/suse
  echo "Kicking off ponyc packaging for COPR..."
  docker run -it --rm -e COPR_LOGIN=${COPR_LOGIN} -e COPR_USERNAME=${COPR_USERNAME} -e COPR_TOKEN=${COPR_TOKEN} -e COPR_COPR_URL=${COPR_COPR_URL} mgruener/copr-cli buildscm --clone-url https://github.com/dipinhora/ponyc --commit ${package_version} --subdir /.packaging/rpm/ --spec ponyc.spec --type git --nowait testcopr
}

ponyc-build-docs(){
  echo "Installing mkdocs and offical theme..."
  sudo -H pip install mkdocs-ponylang

  echo "Building ponyc docs..."
  make CC="$CC1" CXX="$CXX1" docs-online

  echo "Uploading docs using mkdocs..."
  git remote add gh-token "https://${STDLIB_TOKEN}@github.com/ponylang/stdlib.ponylang.org"
  git fetch gh-token
  git reset gh-token/master
  cd stdlib-docs
  mkdocs gh-deploy -v --clean --remote-name gh-token --remote-branch master
}

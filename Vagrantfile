Vagrant.configure("2") do |config|
  config.vm.box = "lxb/ubuntu-14.04-i386"
  config.vm.boot_timeout = 600
  config.vm.provider :libvirt do |libvirt|
    libvirt.driver = "qemu"
    libvirt.memory = 3072
    libvirt.cpus = 2
    libvirt.cpu_mode = "host-model"
  end
end

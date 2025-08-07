{
  pkgs ? import (fetchGit {
    name = "nixos-25.05";
    url = "https://github.com/nixos/nixpkgs/";
    ref = "refs/heads/nixos-25.05";
    # `git ls-remote https://github.com/nixos/nixpkgs nixos-21.11`
    rev = "ce01daebf8489ba97bd1609d185ea276efdeb121";
  }) {}
}:

with pkgs;
mkShell {
  buildInputs = [
    gnumake
    gcc-arm-embedded
    dfu-util

    blackmagic
    pkg-config
    libftdi1
    libusb-compat-0_1
    hidapi

    (python3.withPackages (python-packages: with python-packages; [
      pyusb
      pyserial
    ]))
  ];
}

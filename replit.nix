{pkgs}: {
  deps = [
    pkgs.libsecret
    pkgs.openssl
    pkgs.curl
    pkgs.glfw
    pkgs.pkg-config
    pkgs.cmake
  ];
}

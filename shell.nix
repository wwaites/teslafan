with import <nixpkgs> {};
stdenv.mkDerivation {
  name = "teslafan";
  buildInputs = [
    pkgs.cudatoolkit
    pkgs.platformio
  ];
}

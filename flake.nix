{
  description = "Fluid Sim";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          clang
          premake5
          pkg-config
          raylib
          vulkan-loader
          vulkan-headers
          vulkan-validation-layers
          vulkan-utility-libraries
          vulkan-memory-allocator
          vk-bootstrap
          glfw3
          shaderc          # glslc
          glslang          # glslangValidator
          spirv-tools      # spirv-val, spirv-dis
          vulkan-tools     # vulkaninfo
          gdb
        ];

        # mkShell exports these as environment variables
        LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [
          pkgs.vulkan-loader
          pkgs.glfw3
        ];

        VK_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";
      };
    };
}

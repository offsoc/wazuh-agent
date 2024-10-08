# Build Instructions

The following dependencies are required for this project:

- **Git**
- **CMake** at least 3.22
- **Make**
- **C++ compiler** (GCC 10 or Clang 13, coroutines support needed)
- **Zip** (for [vcpkg](https://vcpkg.io))
- **Curl** (for [vcpkg](https://vcpkg.io))
- **Tar** (for [vcpkg](https://vcpkg.io))
- **Ninja-build** (for [vcpkg](https://vcpkg.io))
- **Pkg-config**

## Compilation steps

1. **Installing Dependencies on Debian**

    To install the necessary dependencies on a Debian-based system, run the following commands:

    ```bash
    sudo apt-get update
    sudo apt-get install cmake make gcc git zip curl tar ninja-build pkg-config
    ```

2. **Clone the Repository**

    First, clone the repository using the following command:

    ```bash
    git clone https://github.com/wazuh/wazuh-agent.git
    ```

3. **Initialize Submodules**

    The project uses submodules, so you need to initialize and update them. Run the following commands:

    ```bash
    cd wazuh-agent
    git submodule update --init --recursive
    ```

4. **Configure and Build the Project**

    ```bash
    cmake src -B build
    cmake --build build
    ```

    If you want to include tests, configure the project with the following command:

    ```bash
    cmake src -B build -DBUILD_TESTS=1
    cmake --build build
    ```

5. **Run tests**

    If built with CMake and `-DBUILD_TESTS=1`, you can run tests with:

    ```bash
    ctest --test-dir build --output-log build
    ```

## Notes

- The project uses `vcpkg` as a submodule to manage dependencies. By initializing the submodules, `vcpkg` will automatically fetch the necessary dependencies when running CMake.

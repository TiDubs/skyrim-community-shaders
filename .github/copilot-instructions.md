# Skyrim Community Shaders

**ALWAYS follow these instructions first and only fallback to additional search and context gathering if the information is incomplete or found to be in error.**

SKSE64 plugin providing modular DirectX 11 graphics enhancements for Skyrim SE/AE/VR. Features runtime shader compilation, 25+ graphics features, and cross-platform Skyrim variant support.

## Critical Build Requirements

**This is a Windows-only project requiring specific toolchain setup:**

- Visual Studio Community 2022 with "Desktop development with C++" workload
- CMake 3.21+ (add to PATH environment variable)
- Git (add to PATH environment variable)  
- vcpkg with VCPKG_ROOT environment variable set
- Windows SDK
- **NEVER CANCEL BUILDS**: Full builds take 45-60 minutes, shader validation takes 15-30 minutes

## Environment Limitations

**In Linux/WSL environments:**
- **Cannot build natively** - Windows-only with Visual Studio dependency
- **Cannot validate shaders** - requires Windows fxc.exe DirectX shader compiler  
- **Docker limitation** - requires Windows Docker containers (Windows host only)
- **Repository tasks work** - can clone, examine code, run Python analysis tools
- **Use case**: Code review, documentation, Python tooling only

## Working Effectively

### Initial Repository Setup
```bash
# Clone with submodules (CRITICAL - takes 2-5 minutes)
git clone https://github.com/doodlum/skyrim-community-shaders.git --recursive
cd skyrim-community-shaders

# If cloned without --recursive, initialize submodules
git submodule update --init --recursive
```

### Build Commands (Windows Only)
```powershell
# Primary build command - NEVER CANCEL: Takes 45-60 minutes
./BuildRelease.bat [PRESET]

# Available presets:
# ALL (default) - Universal binary for SE/AE/VR
# SE - Skyrim Special Edition only  
# AE - Anniversary Edition only
# VR - Skyrim VR only
# ALL-TRACY - With Tracy profiler support
```

**CRITICAL TIMING REQUIREMENTS:**
- **Build time**: 45-60 minutes - NEVER CANCEL. Set timeout to 90+ minutes.
- **Shader validation**: 15-30 minutes - NEVER CANCEL. Set timeout to 45+ minutes.
- **Submodule init**: 2-5 minutes
- **CMake configure**: 5-10 minutes

### Development Setup (Optional)
```powershell
# Copy template for customization
copy CMakeUserPresets.json.template CMakeUserPresets.json

# Edit CMakeUserPresets.json to configure:
# - CommunityShadersOutputDir: Auto-deploy to Skyrim installation directories
# - AUTO_PLUGIN_DEPLOYMENT: ON/OFF for automatic deployment
# - AIO_ZIP_TO_DIST: ON (creates all-in-one package)  
# - ZIP_TO_DIST: ON (creates individual feature packages)
# - TRACY_SUPPORT: ON/OFF for performance profiling
```

## Shader Development and Validation

### Install hlslkit (Python tool for shader validation)
```bash
pip install git+https://github.com/alandtse/hlslkit.git
```

### Shader Validation Workflow
```bash
# STEP 1: Prepare shaders (builds directory structure)
cmake --build ./build/ALL --target prepare_shaders

# STEP 2: Full shader validation - NEVER CANCEL: Takes 15-30 minutes
hlslkit-compile --shader-dir build/ALL/aio/Shaders --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml --max-warnings 0 --suppress-warnings X1519

# VR-specific validation  
hlslkit-compile --shader-dir build/ALL/aio/Shaders --output-dir build/ShaderCache --config .github/configs/shader-validation-vr.yaml --max-warnings 0 --suppress-warnings X1519

# Targeted testing for development (faster, recommended during active development):

# Test specific base shader
hlslkit-compile --shader-dir build/ALL/aio/Shaders/Lighting.hlsl --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml

# Test specific feature directory
hlslkit-compile --shader-dir build/ALL/aio/Shaders/ScreenSpaceGI/ --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml

# Test compute shader
hlslkit-compile --shader-dir build/ALL/aio/Shaders/LightLimitFix/ClusterBuildingCS.hlsl --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml
```

### Additional Shader Tools
```bash
# Generate shader defines from game log (requires CommunityShaders.log)
hlslkit-generate --log CommunityShaders.log --output shader-defines.yaml

# Scan for HLSL buffer conflicts
hlslkit-buffer-scan --show-conflicts
```

## Validation Scenarios

**ALWAYS test these scenarios after making changes:**

1. **Build Validation**: Complete clean build succeeds
2. **Shader Compilation**: No shader compilation errors or new warnings
3. **Feature Loading**: Features load without crashes in test environment
4. **Cross-Platform**: Changes work across SE/AE/VR variants

**Manual Testing Approaches:**
- Build with different presets (SE, AE, VR) to test compatibility
- Run shader validation on modified features
- Check that feature configurations load properly
- Verify no new compilation warnings introduced

## Code Navigation

### Key Directories (29 features total)
- `src/` - Main C++ plugin implementation (~50 files)
  - `src/Features/` - Individual graphics feature implementations (25+ features)
  - `src/Feature.h/.cpp` - Base class for all features
  - `src/State.h/.cpp` - Global feature lifecycle management
  - `src/ShaderCache.h/.cpp` - Runtime shader compilation system
  - `src/Menu/` - ImGui-based in-game configuration interface
- `features/` - 29 graphics features including:
  - Screen Space GI, Volumetric Lighting, Dynamic Cubemaps
  - Light Limit Fix, IBL, Terrain Shadows, Water Effects
  - Extended Materials, Subsurface Scattering, etc.
  - `features/*/Shaders/` - Feature-specific HLSL shader files (~1850 lines total)
  - `features/*/Shaders/Features/*.ini` - Versioned configuration files  
- `package/Shaders/` - Base shader files (50+ core shaders)
  - `Lighting.hlsl`, `Water.hlsl`, `Sky.hlsl`, etc.
- `template/` - New feature template files (NewFeature.h/.cpp, template shader structure)
- `extern/` - Critical dependencies:
  - `CommonLibSSE-NG/` - SKSE framework  
  - `FidelityFX-SDK/` - AMD graphics technologies
  - `Streamline-DX12/` - NVIDIA technologies

### Frequently Accessed Files
```bash
# Core architecture
src/Feature.h              # Base Feature class interface
src/State.cpp              # Feature registration and lifecycle  
src/Globals.h              # Global feature access points
src/ShaderCache.cpp        # Runtime shader compilation
src/Hooks.cpp              # DirectX API interception

# Main shaders (most commonly modified)
package/Shaders/Lighting.hlsl      # Core lighting calculations
package/Shaders/Water.hlsl         # Water rendering
package/Shaders/Sky.hlsl           # Sky and atmospheric effects
package/Shaders/Utility.hlsl       # Utility functions and shadow rendering

# Feature examples (for reference)
features/Screen\ Space\ GI/        # Complex compute shader feature
features/Light\ Limit\ Fix/        # Performance optimization feature  
features/Volumetric\ Lighting/     # Atmospheric effect feature
template/NewFeature.h              # Template for new features

# Configuration and validation
.github/configs/shader-validation.yaml    # Shader compilation settings
CMakePresets.json                         # Build configuration presets
vcpkg.json                               # Dependency specifications
.pre-commit-config.yaml                  # Code formatting rules
```

### Architecture Patterns
- **Feature System**: Each graphics enhancement is a `Feature` class that can be enabled/disabled
- **Runtime Targeting**: Single binary supports SE/AE/VR through runtime detection  
- **Shader Architecture**: Base shaders + feature-specific shader additions
- **DirectX Hooking**: Uses Detours to intercept DirectX 11 API calls in `src/Hooks.cpp`

## Validation and CI Requirements

### Pre-commit Validation (Windows)
```bash  
# Install pre-commit if not available
pip install pre-commit

# ALWAYS run before committing - formatting and linting enforced by CI
pre-commit run --all-files

# Manual equivalents if pre-commit unavailable:
# clang-format (requires clang-format in PATH)
find . -name "*.h" -o -name "*.hpp" -o -name "*.c" -o -name "*.cpp" -o -name "*.hlsl" -o -name "*.hlsli" | xargs clang-format -i

# prettier (requires Node.js and prettier)  
npx prettier --write "**/*.{json,md,yml,yaml}"
```

### CI Build Validation
The GitHub Actions workflow runs:
1. **C++ Build**: 45-60 minutes on windows-2022 runners
2. **Shader Validation**: 15-30 minutes with comprehensive testing
3. **Caching**: Aggressive build cache optimization (check for cache hits)
4. **Multi-target**: Validates SE/AE/VR compatibility

## Common Tasks

### Adding a New Graphics Feature
1. **Copy template files**: 
   ```bash
   cp template/NewFeature.h template/NewFeature.cpp src/Features/YourFeature.h src/Features/YourFeature.cpp
   cp -r "template/New Feature" "features/Your Feature"
   ```
2. **Replace all "NewFeature" occurrences** in .h and .cpp files with your feature name
3. **Update metadata** in both files (author, description, version)
4. **Register feature**: Add singleton to `src/Feature.cpp`, `Globals.h`, and `Globals.cpp`
5. **Configure feature**: Update `features/Your Feature/Shaders/Features/YourFeature.ini`
6. **Run full validation suite** - NEVER CANCEL: 15-30 minutes
7. **Test across variants**: Build with SE, AE, VR presets

### Modifying Existing Shaders  
1. Edit shader files in `features/*/Shaders/` or `package/Shaders/`
2. Run targeted shader validation: `hlslkit-compile --shader-dir [specific-shader-or-dir]`
3. Test with multiple shader defines combinations
4. Verify no new warnings introduced

### Performance Analysis
1. Build with `ALL-TRACY` preset for Tracy profiler support
2. Enable `TRACY_SUPPORT: ON` in CMakeUserPresets.json
3. May require cleaning build directory when toggling Tracy support

## Platform-Specific Notes

### Windows Development
- **Required**: Visual Studio 2022, vcpkg, Windows SDK
- **Build**: Use `./BuildRelease.bat` directly
- **Shader**: Full fxc.exe support for validation

### Docker (Windows Containers Only)
```powershell
# Switch to Windows containers and build image
& 'C:\Program Files\Docker\Docker\DockerCli.exe' -SwitchWindowsEngine
docker build -t skyrim-community-shaders .

# Run build (may take 60+ minutes)
docker run -it --rm -v .:C:/skyrim-community-shaders skyrim-community-shaders:latest

# If access violations occur:
docker run -it --rm --isolation=process -v .:C:/skyrim-community-shaders skyrim-community-shaders:latest
```

### Linux/WSL Limitations
- **Cannot build natively** - Windows-only project
- **Shader validation fails** - requires Windows fxc.exe
- **Alternative**: Use Windows environment or document limitations
- **Repository tasks**: Can clone, examine code, run Python scripts, but cannot build or validate shaders

## Troubleshooting

### Build Issues
- **Long build times**: Normal - builds take 45-60 minutes, never cancel
- **Cache issues**: Remove `build/` directory if switching presets or after long periods
- **vcpkg errors**: Ensure VCPKG_ROOT is set and matches baseline in vcpkg.json
- **Submodule issues**: Run `git submodule update --init --recursive`

### Shader Issues
- **Compilation errors**: Check feature-specific shader defines in `.github/configs/shader-validation.yaml`
- **Warning threshold**: Use `--max-warnings 0` to catch new warnings
- **X1519 warnings**: Suppress with `--suppress-warnings X1519` (known acceptable warnings)

## Summary Commands Reference
```powershell
# Essential workflow:
git clone https://github.com/doodlum/skyrim-community-shaders.git --recursive
cd skyrim-community-shaders

# Build (45-60 min, NEVER CANCEL)
./BuildRelease.bat

# Shader validation (15-30 min, NEVER CANCEL)  
cmake --build ./build/ALL --target prepare_shaders
hlslkit-compile --shader-dir build/ALL/aio/Shaders --output-dir build/ShaderCache --config .github/configs/shader-validation.yaml --max-warnings 0 --suppress-warnings X1519

# Pre-commit validation
pre-commit run --all-files
```

**Remember: This is a complex graphics programming project with long build times. Plan accordingly and never cancel running builds or shader validation.**
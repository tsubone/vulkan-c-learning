# Copilot Instructions for Vulkan C Learning Project

## Project Overview
This is a Vulkan learning project written in C. The goal is to understand Vulkan API through simple examples.

## Coding Guidelines
- Use C99 standard.
- Follow Vulkan best practices for resource management.
- Include error checking for Vulkan calls.
- Use descriptive variable names.
- Comment complex Vulkan operations.

## File Structure
- `main.c`: Main application code.
- `triangle.vert` and `triangle.frag`: GLSL shader sources.
- Avoid committing build artifacts (.spv files, build directories).

## Specific Instructions
- When suggesting code, ensure Vulkan objects are properly initialized and destroyed.
- Prefer explicit error handling over assertions.
- Use Vulkan validation layers in debug builds.
- Suggest improvements for performance and correctness.

## Tools and Dependencies
- Vulkan SDK required.
- Visual Studio for building on Windows.
- GLSL compiler for shaders.
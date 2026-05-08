"use strict";

function effectivePlatform(env, processPlatform) {
  return env.SLOPPY_RUNTIME_PLATFORM || processPlatform;
}

function effectiveArch(env, processArch) {
  return env.SLOPPY_RUNTIME_ARCH || processArch;
}

function effectiveLibc(env) {
  return env.SLOPPY_RUNTIME_LIBC || "gnu";
}

function resolvePlatformPackage(env, processPlatform, processArch) {
  const platform = effectivePlatform(env, processPlatform);
  const arch = effectiveArch(env, processArch);
  const libc = effectiveLibc(env);

  if (platform === "win32" && arch === "x64") {
    return { supported: true, packageName: "@sloppy/runtime-win32-x64" };
  }
  if (platform === "linux" && arch === "x64" && libc === "gnu") {
    return { supported: true, packageName: "@sloppy/runtime-linux-x64-gnu" };
  }
  if (platform === "darwin" && arch === "arm64") {
    return { supported: true, packageName: "@sloppy/runtime-darwin-arm64" };
  }
  if (platform === "darwin" && arch === "x64") {
    return { supported: true, packageName: "@sloppy/runtime-darwin-x64" };
  }

  return {
    supported: false,
    message:
      `Unsupported Sloppy runtime npm platform: platform=${platform} arch=${arch} libc=${libc}. ` +
      "Use a GitHub Release archive or build Sloppy from source for this platform."
  };
}

module.exports = { resolvePlatformPackage };

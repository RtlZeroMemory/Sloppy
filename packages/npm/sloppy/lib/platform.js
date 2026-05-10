"use strict";

function effectivePlatform(env, processPlatform) {
  return env.SLOPPY_RUNTIME_PLATFORM || processPlatform;
}

function effectiveArch(env, processArch) {
  return env.SLOPPY_RUNTIME_ARCH || processArch;
}

function effectiveLibc(env, platform) {
  if (env.SLOPPY_RUNTIME_LIBC) {
    return env.SLOPPY_RUNTIME_LIBC;
  }
  if (platform !== "linux") {
    return "";
  }
  if (
    typeof process !== "undefined" &&
    process.report &&
    typeof process.report.getReport === "function"
  ) {
    const report = process.report.getReport();
    if (report && report.header && report.header.glibcVersionRuntime) {
      return "gnu";
    }
  }
  return "musl";
}

function resolvePlatformPackage(env, processPlatform, processArch) {
  const platform = effectivePlatform(env, processPlatform);
  const arch = effectiveArch(env, processArch);
  const libc = effectiveLibc(env, platform);

  if (platform === "win32" && arch === "x64") {
    return { supported: true, packageName: "@rtlzeromemory/sloppy-win32-x64" };
  }
  if (platform === "linux" && arch === "x64" && libc === "gnu") {
    return { supported: true, packageName: "@rtlzeromemory/sloppy-linux-x64" };
  }
  if (platform === "darwin" && arch === "arm64") {
    return { supported: true, packageName: "@rtlzeromemory/sloppy-darwin-arm64" };
  }
  if (platform === "darwin" && arch === "x64") {
    return { supported: true, packageName: "@rtlzeromemory/sloppy-darwin-x64" };
  }

  return {
    supported: false,
    message:
      `Unsupported Sloppy runtime npm platform: platform=${platform} arch=${arch} libc=${libc}. ` +
      "Use a GitHub Release archive or build Sloppy from source for this platform."
  };
}

module.exports = { resolvePlatformPackage };

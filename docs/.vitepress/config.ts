import { defineConfig } from "vitepress";

export default defineConfig({
  title: "Sloppy",
  description: "Compiler-first TypeScript backend runtime and app framework.",
  base: "/Slop/",
  cleanUrls: true,
  lastUpdated: true,
  srcExclude: ["skills/**"],
  themeConfig: {
    logo: "/logo.svg",
    search: {
      provider: "local"
    },
    nav: [
      { text: "Quickstart", link: "/quickstart" },
      { text: "Templates", link: "/guide/templates" },
      { text: "API", link: "/api/" },
      { text: "CLI", link: "/cli/" },
      { text: "Roadmap", link: "/roadmap" },
      { text: "GitHub", link: "https://github.com/RtlZeroMemory/Slop" }
    ],
    sidebar: [
      {
        text: "Start Here",
        items: [
          { text: "What is Sloppy?", link: "/about/what-is-sloppy" },
          { text: "Install", link: "/install" },
          { text: "Quickstart", link: "/quickstart" },
          { text: "Templates", link: "/guide/templates" },
          { text: "Create your first API", link: "/tutorials/first-api" },
          { text: "Current status", link: "/reference/stability" }
        ]
      },
      {
        text: "Tutorials",
        items: [
          { text: "Overview", link: "/tutorials/" },
          { text: "Build your first API", link: "/tutorials/first-api" },
          { text: "Build a multi-file API", link: "/tutorials/multi-file-api" },
          { text: "Config, services, and logging", link: "/tutorials/config-services-logging" },
          { text: "Middleware, CORS, and request logging", link: "/tutorials/middleware-cors-request-logging" },
          { text: "Generate OpenAPI and package", link: "/tutorials/openapi-and-package" }
        ]
      },
      {
        text: "Guides",
        items: [
          { text: "Templates", link: "/guide/templates" },
          { text: "Project layout", link: "/guide/project-layout" },
          { text: "Program Mode", link: "/guide/program-mode" },
          { text: "Using installed packages", link: "/guide/using-packages" },
          { text: "Native FFI", link: "/guide/native-ffi" },
          { text: "SQLite walkthrough", link: "/guide/sqlite" },
          { text: "Request logging", link: "/guide/request-logging" },
          { text: "TypeScript source input", link: "/guide/typescript" },
          { text: "Plan model", link: "/guide/plan-model" },
          { text: "Examples", link: "/guide/examples" },
          { text: "Testing apps", link: "/api/testing" },
          { text: "Troubleshooting", link: "/guide/troubleshooting" },
          { text: "Sloppy vs Node/Bun/Deno", link: "/about/sloppy-vs-node-bun-deno" },
          { text: "Performance", link: "/about/performance" },
          { text: "Why node_modules is build input", link: "/about/why-no-node-modules" }
        ]
      },
      {
        text: "API",
        items: [
          { text: "App", link: "/api/app" },
          { text: "Routing", link: "/api/routing" },
          { text: "Middleware", link: "/api/middleware" },
          { text: "CORS", link: "/api/cors" },
          { text: "Health checks", link: "/api/health" },
          { text: "ProblemDetails", link: "/api/problem-details" },
          { text: "Request IDs", link: "/api/request-id" },
          { text: "Request logging", link: "/api/request-logging" },
          { text: "Request context", link: "/api/request-context" },
          { text: "Results", link: "/api/results" },
          { text: "Services", link: "/api/services" },
          { text: "Config", link: "/api/config" },
          { text: "Logging", link: "/api/logging" },
          { text: "Data", link: "/api/data" },
          { text: "HTTP client / Network", link: "/api/http-client" },
          { text: "Filesystem", link: "/api/filesystem" },
          { text: "OS / Process boundary", link: "/api/os" },
          { text: "Time", link: "/api/time" },
          { text: "Crypto", link: "/api/crypto" },
          { text: "Codec", link: "/api/codec" },
          { text: "Workers", link: "/api/workers" },
          { text: "Schema", link: "/api/schema" },
          { text: "Testing", link: "/api/testing" }
        ]
      },
      {
        text: "CLI",
        items: [
          { text: "Overview", link: "/cli/" },
          { text: "create", link: "/cli/create" },
          { text: "build", link: "/cli/build" },
          { text: "run", link: "/cli/run" },
          { text: "package", link: "/cli/package" },
          { text: "routes", link: "/cli/routes" },
          { text: "deps", link: "/cli/deps" },
          { text: "capabilities", link: "/cli/capabilities" },
          { text: "doctor", link: "/cli/doctor" },
          { text: "audit", link: "/cli/audit" },
          { text: "openapi", link: "/cli/openapi" },
          { text: "sloppyc", link: "/cli/sloppyc" }
        ]
      },
      {
        text: "Reference",
        items: [
          { text: "Stability matrix", link: "/reference/stability" },
          { text: "Platform status", link: "/reference/platform-status" },
          { text: "Supported syntax", link: "/reference/supported-syntax" },
          { text: "Plan format", link: "/reference/plan-format" },
          { text: "sloppy.json", link: "/reference/sloppy-json" },
          { text: "Configuration keys", link: "/reference/configuration" },
          { text: "Dependency graph", link: "/reference/dependency-graph" },
          { text: "Node compatibility", link: "/reference/node-compatibility" },
          { text: "Native FFI", link: "/reference/ffi" },
          { text: "Native dependencies", link: "/reference/dependencies" },
          { text: "Diagnostics", link: "/reference/diagnostics" },
          { text: "Release artifact contract", link: "/release/artifact-contract" }
        ]
      },
      {
        text: "Internals",
        items: [
          { text: "Architecture", link: "/internals/architecture" },
          { text: "Compiler", link: "/internals/compiler" },
          { text: "Runtime", link: "/internals/runtime" },
          { text: "HTTP runtime", link: "/internals/http-runtime" },
          { text: "V8 bridge", link: "/internals/v8-bridge" },
          { text: "Provider runtime", link: "/internals/provider-runtime" },
          { text: "Async runtime", link: "/internals/async-runtime" },
          { text: "Memory model", link: "/internals/memory-model" },
          { text: "Platform boundaries", link: "/internals/platform-boundaries" },
          { text: "Security model", link: "/internals/security-model" }
        ]
      },
      {
        text: "Roadmap",
        items: [
          { text: "Roadmap", link: "/roadmap" }
        ]
      }
    ],
    socialLinks: [
      { icon: "github", link: "https://github.com/RtlZeroMemory/Slop" }
    ],
    footer: {
      message: "Pre-alpha runtime. APIs and artifact formats may change.",
      copyright: "Released under the Sloppy project license."
    }
  }
});

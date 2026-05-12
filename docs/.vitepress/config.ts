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
      { text: "Tutorials", link: "/tutorials/" },
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
          { text: "Project layout", link: "/guide/project-layout" },
          { text: "Templates", link: "/guide/templates" },
          { text: "Static assets", link: "/guide/static-assets" },
          { text: "Realtime updates", link: "/guide/realtime" },
          { text: "TypeScript source input", link: "/guide/typescript" },
          { text: "Validation", link: "/guide/validation" },
          { text: "Authentication", link: "/guide/auth" },
          { text: "Error handling", link: "/guide/error-handling" },
          { text: "Cookies", link: "/guide/cookies" },
          { text: "Forms and uploads", link: "/guide/uploads" },
          { text: "Using packages", link: "/guide/using-packages" },
          { text: "Migrations", link: "/guide/migrations" },
          { text: "PostgreSQL", link: "/guide/postgres" },
          { text: "SQL Server", link: "/guide/sqlserver" },
          { text: "Native FFI", link: "/guide/native-ffi" },
          { text: "Program Mode", link: "/guide/program-mode" },
          { text: "Examples and demo app", link: "/guide/examples" },
          { text: "Testing apps", link: "/api/testing" },
          { text: "Troubleshooting", link: "/guide/troubleshooting" },
          { text: "Sloppy vs Node/Bun/Deno", link: "/about/sloppy-vs-node-bun-deno" },
          { text: "Compiler performance", link: "/about/performance" },
          { text: "Why no node_modules?", link: "/about/why-no-node-modules" }
        ]
      },
      {
        text: "API",
        items: [
          { text: "App", link: "/api/app" },
          { text: "Routing", link: "/api/routing" },
          { text: "Static files (experimental)", link: "/api/static-files" },
          { text: "Middleware", link: "/api/middleware" },
          { text: "CORS", link: "/api/cors" },
          { text: "Auth", link: "/api/auth" },
          { text: "Sessions", link: "/api/sessions" },
          { text: "Security headers", link: "/api/security" },
          { text: "Health checks", link: "/api/health" },
          { text: "ProblemDetails", link: "/api/problem-details" },
          { text: "Request IDs", link: "/api/request-id" },
          { text: "Request logging", link: "/api/request-logging" },
          { text: "Request context", link: "/api/request-context" },
          { text: "Results", link: "/api/results" },
          { text: "Realtime (experimental)", link: "/api/realtime" },
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
          { text: "dev", link: "/cli/dev" },
          { text: "run", link: "/cli/run" },
          { text: "package", link: "/cli/package" },
          { text: "doctor", link: "/cli/doctor" },
          { text: "routes", link: "/cli/routes" },
          { text: "deps", link: "/cli/deps" },
          { text: "capabilities", link: "/cli/capabilities" },
          { text: "audit", link: "/cli/audit" },
          { text: "openapi", link: "/cli/openapi" },
          { text: "db", link: "/cli/db" },
          { text: "sloppyc", link: "/cli/sloppyc" }
        ]
      },
      {
        text: "Reference",
        items: [
          { text: "Stability matrix", link: "/reference/stability" },
          { text: "Supported syntax", link: "/reference/supported-syntax" },
          { text: "Plan format", link: "/reference/plan-format" },
          { text: "Configuration keys", link: "/reference/configuration" },
          { text: "Dependency graph", link: "/reference/dependency-graph" },
          { text: "Node compatibility", link: "/reference/node-compatibility" },
          { text: "Dependencies", link: "/reference/dependencies" },
          { text: "Data API", link: "/reference/data-api" },
          { text: "Providers", link: "/reference/providers" },
          { text: "FFI", link: "/reference/ffi" },
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
      message: "Public alpha, pre-production runtime. APIs and artifact formats may change.",
      copyright: "Released under the Sloppy project license."
    }
  }
});

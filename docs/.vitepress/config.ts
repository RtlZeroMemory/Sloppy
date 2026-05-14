import { defineConfig } from "vitepress";

export default defineConfig({
  title: "Sloppy",
  description: "Compiler-first TypeScript runtime and application framework for serious backend apps.",
  base: "/Sloppy/",
  cleanUrls: true,
  lastUpdated: true,
  srcExclude: ["skills/**"],
  themeConfig: {
    logo: "/logo.svg",
    search: {
      provider: "local"
    },
    nav: [
      { text: "Install", link: "/install" },
      { text: "Quickstart", link: "/quickstart" },
      { text: "Guides", link: "/tutorials/" },
      { text: "API", link: "/api/" },
      { text: "CLI", link: "/cli/" },
      { text: "Reference", link: "/reference/stability" },
      { text: "GitHub", link: "https://github.com/RtlZeroMemory/Sloppy" }
    ],
    sidebar: [
      {
        text: "Start",
        items: [
          { text: "What is Sloppy?", link: "/about/what-is-sloppy" },
          { text: "Install", link: "/install" },
          { text: "Quickstart", link: "/quickstart" },
          { text: "Create your first API", link: "/tutorials/first-api" },
          { text: "Examples", link: "/guide/examples" },
          { text: "Current status", link: "/reference/stability" }
        ]
      },
      {
        text: "Framework",
        items: [
          { text: "Project layout", link: "/guide/project-layout" },
          { text: "Templates", link: "/guide/templates" },
          { text: "App", link: "/api/app" },
          { text: "Routing", link: "/api/routing" },
          { text: "Middleware", link: "/api/middleware" },
          { text: "CORS", link: "/api/cors" },
          { text: "Auth", link: "/guide/auth" },
          { text: "Sessions", link: "/api/sessions" },
          { text: "Security headers", link: "/api/security" },
          { text: "Rate limiting", link: "/guide/rate-limiting" },
          { text: "Validation", link: "/guide/validation" },
          { text: "Error handling", link: "/guide/error-handling" },
          { text: "Cookies", link: "/guide/cookies" },
          { text: "Forms and uploads", link: "/guide/uploads" },
          { text: "Static assets", link: "/guide/static-assets" },
          { text: "Realtime updates", link: "/guide/realtime" },
          { text: "WebSockets", link: "/api/websockets" },
          { text: "Webhooks", link: "/guide/webhooks" },
          { text: "Background tasks", link: "/guide/background-tasks" },
          { text: "Durable jobs", link: "/api/jobs" },
          { text: "Data streaming", link: "/guide/data-streaming" },
          { text: "ORM", link: "/api/orm" },
          { text: "Cache", link: "/api/cache" },
          { text: "Redis", link: "/api/redis" },
          { text: "HTTP clients", link: "/api/http-client" },
          { text: "TypeScript source input", link: "/guide/typescript" },
          { text: "Program Mode", link: "/guide/program-mode" }
        ]
      },
      {
        text: "Core APIs",
        items: [
          { text: "API overview", link: "/api/" },
          { text: "Results", link: "/api/results" },
          { text: "Services", link: "/api/services" },
          { text: "Config", link: "/api/config" },
          { text: "Logging", link: "/api/logging" },
          { text: "ProblemDetails", link: "/api/problem-details" },
          { text: "Request IDs", link: "/api/request-id" },
          { text: "Request logging", link: "/api/request-logging" },
          { text: "Request context", link: "/api/request-context" },
          { text: "Rate limit", link: "/api/rate-limit" },
          { text: "Health checks", link: "/api/health" },
          { text: "Metrics", link: "/api/metrics" },
          { text: "Management", link: "/api/management" },
          { text: "Static files", link: "/api/static-files" },
          { text: "Realtime", link: "/api/realtime" },
          { text: "Webhooks", link: "/api/webhooks" },
          { text: "Workers", link: "/api/workers" },
          { text: "Jobs", link: "/api/jobs" },
          { text: "Data", link: "/api/data" },
          { text: "ORM", link: "/api/orm" },
          { text: "Cache", link: "/api/cache" },
          { text: "Redis", link: "/api/redis" },
          { text: "HTTP client / Network", link: "/api/http-client" },
          { text: "Filesystem", link: "/api/filesystem" },
          { text: "OS / Process boundary", link: "/api/os" },
          { text: "Time", link: "/api/time" },
          { text: "Crypto", link: "/api/crypto" },
          { text: "Codec", link: "/api/codec" },
          { text: "Schema", link: "/api/schema" }
        ]
      },
      {
        text: "Testing",
        items: [
          { text: "Testing apps", link: "/api/testing" },
          { text: "TestHost", link: "/api/testhost" },
          { text: "TestServices", link: "/api/testservices" }
        ]
      },
      {
        text: "Operations",
        items: [
          { text: "CLI overview", link: "/cli/" },
          { text: "create", link: "/cli/create" },
          { text: "build", link: "/cli/build" },
          { text: "dev", link: "/cli/dev" },
          { text: "run", link: "/cli/run" },
          { text: "package", link: "/cli/package" },
          { text: "doctor", link: "/cli/doctor" },
          { text: "jobs", link: "/cli/jobs" },
          { text: "routes", link: "/cli/routes" },
          { text: "deps", link: "/cli/deps" },
          { text: "capabilities", link: "/cli/capabilities" },
          { text: "audit", link: "/cli/audit" },
          { text: "openapi", link: "/cli/openapi" },
          { text: "db", link: "/cli/db" },
          { text: "sloppyc", link: "/cli/sloppyc" },
          { text: "Migrations", link: "/guide/migrations" },
          { text: "Health checks", link: "/api/health" },
          { text: "Metrics", link: "/api/metrics" },
          { text: "Management endpoints", link: "/api/management" },
          { text: "Background tasks", link: "/guide/background-tasks" },
          { text: "Troubleshooting", link: "/guide/troubleshooting" }
        ]
      },
      {
        text: "Examples",
        items: [
          { text: "Examples overview", link: "/guide/examples" },
          { text: "Build your first API", link: "/tutorials/first-api" },
          { text: "Build a multi-file API", link: "/tutorials/multi-file-api" },
          { text: "Config, services, and logging", link: "/tutorials/config-services-logging" },
          { text: "Middleware, CORS, and request logging", link: "/tutorials/middleware-cors-request-logging" },
          { text: "Generate OpenAPI and package", link: "/tutorials/openapi-and-package" },
          { text: "Using packages", link: "/guide/using-packages" },
          { text: "PostgreSQL", link: "/guide/postgres" },
          { text: "SQL Server", link: "/guide/sqlserver" },
          { text: "Native FFI", link: "/guide/native-ffi" }
        ]
      },
      {
        text: "Reference",
        items: [
          { text: "Stability matrix", link: "/reference/stability" },
          { text: "Rate limit", link: "/reference/rate-limit" },
          { text: "WebSockets", link: "/reference/websockets" },
          { text: "Webhooks", link: "/reference/webhooks" },
          { text: "Workers", link: "/reference/workers" },
          { text: "Supported syntax", link: "/reference/supported-syntax" },
          { text: "Plan format", link: "/reference/plan-format" },
          { text: "Configuration keys", link: "/reference/configuration" },
          { text: "Dependency graph", link: "/reference/dependency-graph" },
          { text: "Packages", link: "/reference/packages" },
          { text: "Node compatibility", link: "/reference/node-compatibility" },
          { text: "Dependencies", link: "/reference/dependencies" },
          { text: "Data API", link: "/reference/data-api" },
          { text: "Providers", link: "/reference/providers" },
          { text: "Jobs storage", link: "/reference/jobs-storage" },
          { text: "FFI", link: "/reference/ffi" },
          { text: "Diagnostics", link: "/reference/diagnostics" },
          { text: "Sloppy vs Node/Bun/Deno", link: "/about/sloppy-vs-node-bun-deno" },
          { text: "Compiler performance", link: "/about/performance" },
          { text: "Why no node_modules?", link: "/about/why-no-node-modules" }
        ]
      },
      {
        text: "Architecture",
        items: [
          { text: "Architecture", link: "/internals/architecture" },
          { text: "Compiler", link: "/internals/compiler" },
          { text: "Runtime", link: "/internals/runtime" },
          { text: "HTTP runtime", link: "/internals/http-runtime" },
          { text: "WebSocket runtime", link: "/internals/websocket-runtime" },
          { text: "V8 bridge", link: "/internals/v8-bridge" },
          { text: "Provider runtime", link: "/internals/provider-runtime" },
          { text: "Scheduler runtime", link: "/internals/scheduler" },
          { text: "TestServices", link: "/internals/testservices" },
          { text: "Async runtime", link: "/internals/async-runtime" },
          { text: "Memory model", link: "/internals/memory-model" },
          { text: "Platform boundaries", link: "/internals/platform-boundaries" },
          { text: "Security model", link: "/internals/security-model" }
        ]
      },
      {
        text: "Release",
        items: [
          { text: "Release overview", link: "/release/" },
          { text: "Artifact contract", link: "/release/artifact-contract" },
          { text: "Roadmap", link: "/roadmap" }
        ]
      }
    ],
    socialLinks: [
      { icon: "github", link: "https://github.com/RtlZeroMemory/Sloppy" }
    ],
    footer: {
      message: "Public alpha. APIs and artifact formats may still change between alpha revisions.",
      copyright: "Released under the Sloppy project license."
    }
  }
});

use super::*;

#[derive(Clone, Debug)]
pub(super) struct ConfigEntry {
    pub(super) key: String,
    pub(super) value: Value,
    pub(super) source: String,
}

#[derive(Debug)]
pub(super) struct ConfigurationModel {
    pub(super) environment: String,
    pub(super) values: BTreeMap<String, ConfigEntry>,
}

impl ConfigurationModel {
    pub(super) fn load(
        input: &Path,
        options: &CompileOptions,
        config_reads: &[ConfigReadMetadata],
    ) -> Result<Self, Diagnostic> {
        let environment = options
            .environment
            .clone()
            .unwrap_or_else(|| "Development".to_string());
        let config_dir = input.parent().unwrap_or_else(|| Path::new(""));
        let mut model = Self {
            environment,
            values: BTreeMap::new(),
        };

        model.add_defaults();
        model.load_optional_json(&config_dir.join("appsettings.json"), "appsettings.json")?;
        let env_file_name = format!("appsettings.{}.json", model.environment);
        model.load_optional_json(&config_dir.join(&env_file_name), &env_file_name)?;
        model.load_optional_json(
            &config_dir.join("appsettings.local.json"),
            "appsettings.local.json",
        )?;
        let env_local_file_name = format!("appsettings.{}.local.json", model.environment);
        model.load_optional_json(&config_dir.join(&env_local_file_name), &env_local_file_name)?;
        model.load_optional_json(
            &config_dir.join(".sloppy").join("secrets.json"),
            "user-secrets:.sloppy/secrets.json",
        )?;
        let env_secret_file_name =
            format!("user-secrets:.sloppy/secrets.{}.json", model.environment);
        model.load_optional_json(
            &config_dir
                .join(".sloppy")
                .join(format!("secrets.{}.json", model.environment)),
            &env_secret_file_name,
        )?;
        model.apply_environment_variables(config_reads)?;
        model.apply_cli_overrides(options);
        Ok(model)
    }

    pub(super) fn add_defaults(&mut self) {
        for (key, value) in [
            ("Sloppy:Server:Host", json!("127.0.0.1")),
            ("Sloppy:Server:Port", json!(5173)),
            ("Sloppy:Server:MaxConnections", json!(4)),
            ("Sloppy:Server:MaxRequestBodyBytes", json!(8192)),
            ("Sloppy:Server:KeepAliveEnabled", json!(true)),
            ("Sloppy:Server:KeepAliveIdleTimeoutMs", json!(5000)),
            ("Sloppy:Server:MaxRequestsPerConnection", json!(100)),
            ("Sloppy:Server:RequestTimeoutMs", json!(30000)),
            ("Sloppy:Runtime:V8MicrotaskDrainLimit", json!(64)),
        ] {
            self.set(key, value, "built-in defaults");
        }
    }

    pub(super) fn load_optional_json(
        &mut self,
        path: &Path,
        source: &str,
    ) -> Result<(), Diagnostic> {
        let contents = match fs::read_to_string(path) {
            Ok(contents) => contents,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(()),
            Err(error) => {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_CONFIG_READ",
                    format!("failed to read {source}: {error}"),
                )
                .with_path(path));
            }
        };
        let value = serde_json::from_str::<Value>(&contents).map_err(|error| {
            Diagnostic::new(
                "SLOPPYC_E_CONFIG_MALFORMED",
                format!("malformed {source}: {error}"),
            )
            .with_path(path)
        })?;
        let Value::Object(object) = value else {
            return Err(Diagnostic::new(
                "SLOPPYC_E_CONFIG_MALFORMED",
                format!("{source} must contain a JSON object"),
            )
            .with_path(path));
        };
        self.flatten_json_object(Vec::new(), &object, source)
            .map_err(|diagnostic| diagnostic.with_path(path))?;
        Ok(())
    }

    pub(super) fn flatten_json_object(
        &mut self,
        prefix: Vec<String>,
        object: &serde_json::Map<String, Value>,
        source: &str,
    ) -> Result<(), Diagnostic> {
        for (key, value) in object {
            if key.is_empty() {
                let path = if prefix.is_empty() {
                    "<root>".to_string()
                } else {
                    prefix.join(":")
                };
                return Err(Diagnostic::new(
                    "SLOPPYC_E_CONFIG_KEY",
                    format!("{source} contains an empty config key segment under {path}"),
                )
                .with_hint("Config keys must not contain empty path segments."));
            }
            let mut next = prefix.clone();
            next.push(key.clone());
            if let Value::Object(child) = value {
                self.flatten_json_object(next, child, source)?;
            } else {
                let key = next.join(":");
                if let Some((resolved, resolved_source)) =
                    resolve_json_config_value(&key, value, source)?
                {
                    self.set(&key, resolved, &resolved_source);
                }
            }
        }
        Ok(())
    }

    pub(super) fn apply_environment_variables(
        &mut self,
        config_reads: &[ConfigReadMetadata],
    ) -> Result<(), Diagnostic> {
        let mut known_roots = self.known_roots();
        for read in config_reads {
            if let Some(root) = read.key.split(':').next() {
                known_roots.insert(normalize_config_key(root));
            }
        }
        for (name, value) in std::env::vars() {
            let Some(logical) = env_logical_name(&name, &known_roots) else {
                continue;
            };
            if logical.is_empty()
                || logical.contains("___")
                || logical.starts_with('_')
                || logical.split("__").any(|segment| segment.is_empty())
            {
                return Err(Diagnostic::new(
                    "SLOPPYC_E_CONFIG_ENV",
                    format!("invalid Sloppy environment variable name '{name}'"),
                )
                .with_hint(
                    "Use Sloppy__Server__Port or SLOPPY_SLOPPY__SERVER__PORT style names.",
                ));
            }
            let key = logical
                .split("__")
                .map(canonical_config_segment)
                .collect::<Vec<_>>()
                .join(":");
            let parsed = self.parse_env_value(&key, &value)?;
            self.set(&key, parsed, &format!("env:{name}"));
        }
        Ok(())
    }

    pub(super) fn parse_env_value(&self, key: &str, value: &str) -> Result<Value, Diagnostic> {
        match self.get(key).map(|entry| &entry.value) {
            Some(Value::Number(existing)) if existing.is_i64() || existing.is_u64() => {
                let parsed = value.parse::<i64>().map_err(|_| {
                    Diagnostic::new(
                        "SLOPPYC_E_CONFIG_ENV",
                        format!(
                            "environment override for {key} expects an integer, got {}",
                            redact_config_value(key, value)
                        ),
                    )
                })?;
                Ok(json!(parsed))
            }
            Some(Value::Number(_)) => {
                let parsed = value.parse::<f64>().map_err(|_| {
                    Diagnostic::new(
                        "SLOPPYC_E_CONFIG_ENV",
                        format!(
                            "environment override for {key} expects a number, got {}",
                            redact_config_value(key, value)
                        ),
                    )
                })?;
                Ok(json!(parsed))
            }
            Some(Value::Bool(_)) => match value.to_ascii_lowercase().as_str() {
                "true" => Ok(json!(true)),
                "false" => Ok(json!(false)),
                _ => Err(Diagnostic::new(
                    "SLOPPYC_E_CONFIG_ENV",
                    format!(
                        "environment override for {key} expects true or false, got {}",
                        redact_config_value(key, value)
                    ),
                )),
            },
            _ => Ok(json!(value)),
        }
    }

    pub(super) fn apply_cli_overrides(&mut self, options: &CompileOptions) {
        if let Some(host) = &options.host {
            self.set("Sloppy:Server:Host", json!(host), "CLI --host");
        }
        if let Some(port) = options.port {
            self.set("Sloppy:Server:Port", json!(port), "CLI --port");
        }
        for (key, value) in &options.config_overrides {
            if normalize_config_key(key).split(':').any(str::is_empty) {
                continue;
            }
            let parsed = self
                .parse_env_value(key, value)
                .unwrap_or_else(|_| json!(value));
            self.set(key, parsed, "CLI --config");
        }
    }

    pub(super) fn apply_to_app(&self, app: &mut ExtractedApp) -> Result<(), Diagnostic> {
        let mut provider_plans = Vec::new();
        let mut requirements = Vec::new();
        for capability in &mut app.capabilities {
            if capability.capability_kind != "database" {
                continue;
            }
            let provider_name = provider_config_name(capability);
            let prefix = format!("Sloppy:Providers:{}:{provider_name}", capability.provider);
            if capability.provider == "sqlite" && capability.database.is_none() {
                let database_key = format!("{prefix}:database");
                if let Some(database) = self.get_string(&database_key)? {
                    let source = self
                        .get(&database_key)
                        .map(|entry| entry.source.clone())
                        .unwrap_or_else(|| "configuration".to_string());
                    capability.database = Some(database);
                    capability.config_source = Some(source);
                } else if capability.from_provider_use {
                    return Err(Diagnostic::new(
                        "SLOPPYC_E_CONFIG_MISSING_PROVIDER",
                        format!(
                            "{} provider '{provider_name}' is missing required config value {database_key}",
                            capability.provider
                        ),
                    )
                    .with_hint(
                        format!(
                            "Add Sloppy:Providers:{}:<name>:database to appsettings.json or pass inline provider options.",
                            capability.provider
                        ),
                    ));
                }
            }
            if let Some(source) = capability.config_source.clone() {
                provider_plans.push(ConfigurationProviderPlan {
                    provider: capability.provider.clone(),
                    name: provider_name,
                    prefix,
                    source,
                });
            }
            requirements.extend(self.provider_requirements(capability)?);
        }
        requirements.extend(self.config_read_requirements(&app.config_reads));
        let package_manifest = package_manifest_for_requirements(&requirements);

        app.configuration = Some(ConfigurationPlan {
            environment: self.environment.clone(),
            keys: self.plan_keys(),
            providers: provider_plans,
            requirements,
            package_manifest,
        });
        Ok(())
    }

    pub(super) fn provider_requirements(
        &self,
        capability: &DatabaseCapability,
    ) -> Result<Vec<ConfigurationRequirementPlan>, Diagnostic> {
        let provider_name = provider_config_name(capability);
        let prefix = format!("Sloppy:Providers:{}:{provider_name}", capability.provider);
        let contract = match capability.provider.as_str() {
            "sqlite" => Some(("database", "string", false)),
            "postgres" | "sqlserver" => Some(("connectionString", "secret", true)),
            _ => None,
        };
        let Some((field, value_type, sensitive)) = contract else {
            return Ok(Vec::new());
        };
        let key = capability
            .config_key
            .clone()
            .unwrap_or_else(|| format!("{prefix}:{field}"));
        let entry = self.get(&key);
        let status = if capability.database.is_some() || entry.is_some() {
            "present"
        } else {
            "missing"
        };
        Ok(vec![ConfigurationRequirementPlan {
            key,
            value_type: value_type.to_string(),
            required: true,
            sensitive,
            status: status.to_string(),
            source: capability
                .config_source
                .clone()
                .or_else(|| entry.map(|entry| entry.source.clone()))
                .or_else(|| {
                    capability
                        .database
                        .as_ref()
                        .map(|_| "inline provider options".to_string())
                }),
            required_by: source_location_label(
                &capability.source_name,
                &capability.source,
                capability.span,
            ),
            default_value: None,
        }])
    }

    pub(super) fn config_read_requirements(
        &self,
        reads: &[ConfigReadMetadata],
    ) -> Vec<ConfigurationRequirementPlan> {
        reads
            .iter()
            .map(|read| {
                let entry = self.get(&read.key);
                let status = if entry.is_some() {
                    "present"
                } else if read.has_default {
                    "defaulted"
                } else {
                    "missing"
                };
                ConfigurationRequirementPlan {
                    key: canonical_config_key(&read.key),
                    value_type: read.value_type.clone(),
                    required: read.required,
                    sensitive: read.sensitive || config_key_is_sensitive(&read.key),
                    status: status.to_string(),
                    source: entry.map(|entry| entry.source.clone()),
                    required_by: source_location_label(&read.source_name, &read.source, read.span),
                    default_value: read.default_value.clone(),
                }
            })
            .collect()
    }

    pub(super) fn plan_keys(&self) -> Vec<ConfigurationPlanKey> {
        self.values
            .values()
            .map(|entry| {
                let sensitive = config_key_is_sensitive(&entry.key);
                ConfigurationPlanKey {
                    key: entry.key.clone(),
                    source: entry.source.clone(),
                    value: if sensitive {
                        json!("<redacted>")
                    } else {
                        entry.value.clone()
                    },
                    sensitive,
                }
            })
            .collect()
    }

    pub(super) fn get_string(&self, key: &str) -> Result<Option<String>, Diagnostic> {
        let Some(entry) = self.get(key) else {
            return Ok(None);
        };
        match &entry.value {
            Value::String(value) => Ok(Some(value.clone())),
            other => Err(Diagnostic::new(
                "SLOPPYC_E_CONFIG_TYPE",
                format!(
                    "config key {key} from {} expects a string, got {}",
                    entry.source,
                    json_type_name(other)
                ),
            )),
        }
    }

    pub(super) fn get(&self, key: &str) -> Option<&ConfigEntry> {
        self.values.get(&normalize_config_key(key))
    }

    pub(super) fn set(&mut self, key: &str, value: Value, source: &str) {
        self.values.insert(
            normalize_config_key(key),
            ConfigEntry {
                key: canonical_config_key(key),
                value,
                source: source.to_string(),
            },
        );
    }

    pub(super) fn known_roots(&self) -> BTreeSet<String> {
        self.values
            .values()
            .filter_map(|entry| entry.key.split(':').next().map(normalize_config_key))
            .collect()
    }
}

pub(super) fn resolve_json_config_value(
    key: &str,
    value: &Value,
    source: &str,
) -> Result<Option<(Value, String)>, Diagnostic> {
    let Value::String(text) = value else {
        return Ok(Some((value.clone(), source.to_string())));
    };
    let Some(name) = env_placeholder_name(text) else {
        return Ok(Some((value.clone(), source.to_string())));
    };
    if name.contains(':') || name.contains("__") || name.trim().is_empty() {
        return Err(Diagnostic::new(
            "SLOPPYC_E_CONFIG_ENV",
            format!("{source} contains invalid environment placeholder for {key}"),
        )
        .with_hint("Use ${NAME} placeholders with a single environment variable name."));
    }
    match std::env::var(name) {
        Ok(resolved) => Ok(Some((json!(resolved), format!("{source}:${{{name}}}")))),
        Err(_) => Ok(None),
    }
}

pub(super) fn env_placeholder_name(value: &str) -> Option<&str> {
    value
        .strip_prefix("${")
        .and_then(|inner| inner.strip_suffix('}'))
}

pub(super) fn env_logical_name(name: &str, known_roots: &BTreeSet<String>) -> Option<String> {
    if let Some(stripped) = name.strip_prefix("SLOPPY_") {
        if stripped.contains("__") {
            return Some(stripped.to_string());
        }
    }
    if !name.contains("__") {
        return None;
    }
    let root = name.split("__").next().unwrap_or_default();
    if normalize_config_key(root) == "SLOPPY" || known_roots.contains(&normalize_config_key(root)) {
        return Some(name.to_string());
    }
    None
}

pub(super) fn source_location_label(source_name: &str, source: &str, span: Span) -> String {
    let (line, _) = line_column(source, span.start);
    format!("{source_name}:{line}")
}

pub(super) fn package_manifest_for_requirements(
    requirements: &[ConfigurationRequirementPlan],
) -> ConfigurationPackageManifest {
    let mut manifest = ConfigurationPackageManifest::default();
    let mut seen_required = BTreeSet::new();
    let mut seen_optional = BTreeSet::new();

    for requirement in requirements {
        let entry = ConfigurationPackageEntry {
            key: requirement.key.clone(),
            env: config_key_to_env_name(&requirement.key),
            value_type: requirement.value_type.clone(),
            sensitive: requirement.sensitive,
            default_value: requirement.default_value.clone(),
        };
        if requirement.required && !requirement.has_default() {
            if seen_required.insert(requirement.key.clone()) {
                manifest.required.push(entry);
            }
        } else if seen_optional.insert(requirement.key.clone()) {
            manifest.optional.push(entry);
        }
    }

    manifest
}

impl ConfigurationRequirementPlan {
    pub(super) fn has_default(&self) -> bool {
        self.default_value.is_some()
    }
}

pub(super) fn config_key_to_env_name(key: &str) -> String {
    key.split(':').collect::<Vec<_>>().join("__")
}

pub(super) fn normalize_config_key(key: &str) -> String {
    key.split(':')
        .map(|segment| segment.to_ascii_uppercase())
        .collect::<Vec<_>>()
        .join(":")
}

pub(super) fn canonical_config_key(key: &str) -> String {
    key.split(':')
        .map(canonical_config_segment)
        .collect::<Vec<_>>()
        .join(":")
}

pub(super) fn canonical_config_segment(segment: &str) -> String {
    match segment.to_ascii_uppercase().as_str() {
        "SLOPPY" => "Sloppy".to_string(),
        "SERVER" => "Server".to_string(),
        "TLS" => "Tls".to_string(),
        "RUNTIME" => "Runtime".to_string(),
        "PROVIDERS" => "Providers".to_string(),
        "SQLITE" => "sqlite".to_string(),
        "POSTGRES" => "postgres".to_string(),
        "POSTGRESQL" => "postgres".to_string(),
        "SQLSERVER" => "sqlserver".to_string(),
        "HOST" => "Host".to_string(),
        "PORT" => "Port".to_string(),
        "MAXCONNECTIONS" => "MaxConnections".to_string(),
        "MAXREQUESTBODYBYTES" => "MaxRequestBodyBytes".to_string(),
        "KEEPALIVEENABLED" => "KeepAliveEnabled".to_string(),
        "KEEPALIVEIDLETIMEOUTMS" => "KeepAliveIdleTimeoutMs".to_string(),
        "MAXREQUESTSPERCONNECTION" => "MaxRequestsPerConnection".to_string(),
        "REQUESTTIMEOUTMS" => "RequestTimeoutMs".to_string(),
        "ENABLED" => "Enabled".to_string(),
        "CERTIFICATEPATH" => "CertificatePath".to_string(),
        "PRIVATEKEYPATH" => "PrivateKeyPath".to_string(),
        "PRIVATEKEY" => "privateKey".to_string(),
        "PASSPHRASE" => "Passphrase".to_string(),
        "V8MICROTASKDRAINLIMIT" => "V8MicrotaskDrainLimit".to_string(),
        "DATABASE" => "database".to_string(),
        "CONNECTIONSTRING" => "connectionString".to_string(),
        "CONNECTION_STRING" => "connectionString".to_string(),
        "APIKEY" => "apiKey".to_string(),
        "API_KEY" => "apiKey".to_string(),
        "CLIENTSECRET" => "clientSecret".to_string(),
        "CLIENT_SECRET" => "clientSecret".to_string(),
        "QUEUECAPACITY" => "queueCapacity".to_string(),
        _ => segment.to_string(),
    }
}

pub(super) fn provider_config_name(capability: &DatabaseCapability) -> String {
    capability
        .config_name
        .clone()
        .unwrap_or_else(|| provider_name_from_token(&capability.token))
}

pub(super) fn provider_name_from_token(token: &str) -> String {
    token.strip_prefix("data.").unwrap_or(token).to_string()
}

pub(super) fn json_type_name(value: &Value) -> &'static str {
    match value {
        Value::Null => "null",
        Value::Bool(_) => "bool",
        Value::Number(_) => "number",
        Value::String(_) => "string",
        Value::Array(_) => "array",
        Value::Object(_) => "object",
    }
}

pub(super) fn config_key_is_sensitive(key: &str) -> bool {
    key.to_ascii_lowercase().split(':').any(|segment| {
        matches!(
            segment,
            "pwd"
                | "passwd"
                | "secret"
                | "password"
                | "token"
                | "apikey"
                | "api_key"
                | "clientsecret"
                | "client_secret"
                | "privatekey"
                | "private_key"
                | "passphrase"
                | "connectionstring"
                | "connection_string"
        ) || segment.ends_with("secret")
            || segment.ends_with("password")
            || segment.ends_with("token")
            || segment.ends_with("apikey")
            || segment.ends_with("clientsecret")
            || segment.ends_with("privatekey")
            || segment.ends_with("passphrase")
            || segment.ends_with("connectionstring")
    })
}

pub(super) fn redact_config_value(key: &str, value: &str) -> String {
    if config_key_is_sensitive(key) {
        "<redacted>".to_string()
    } else {
        format!("'{value}'")
    }
}

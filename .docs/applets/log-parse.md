# log_parse

Structured log parser and JSONL filter.

## Parse Mode

```text
log_parse --regex <pattern> --fields a,b,c --format json|csv
```

- Reads lines from stdin.
- Applies POSIX extended regex.
- Maps capture groups to field names.
- Writes JSON Lines or CSV to stdout.

## Filter Mode

```text
log_parse --filter key=value
```

- Reads JSON Lines from stdin.
- Keeps records with matching flat string/number/bool field.
- Used in pipeline as `log_parse --filter type=clip`.

## Stream Rule

- stdout: data only.
- stderr: diagnostics only.

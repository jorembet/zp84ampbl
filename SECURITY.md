# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| Latest  | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability, please report it responsibly:

### Preferred: GitHub Security Advisories (Private)
1. Go to the **Security** tab of this repository
2. Click **"Report a vulnerability"**
3. Fill in the details privately — only maintainers see it

### Alternative: Email
If GitHub reporting isn't available, email: **security@yourdomain.com**

### What to Include
- Description of the vulnerability
- Steps to reproduce (minimal PoC if possible)
- Affected versions/components
- Potential impact
- Suggested fix (if any)

## Response Timeline

| Stage | Target |
|-------|--------|
| Acknowledgment | 48 hours |
| Initial assessment | 5 business days |
| Fix development | Varies by severity |
| Coordinated disclosure | After fix released |

## Disclosure Policy

- Vulnerabilities are disclosed **after a fix is available** (or mitigation documented)
- Credit given to reporters who follow responsible disclosure
- No bounty program currently — this is a best-effort open source project

## Security Best Practices for Contributors

- Never commit secrets, keys, or tokens
- Use `.env.example` for required environment variables
- Run `npm audit` / `cargo audit` / equivalent before PRs
- Keep dependencies updated
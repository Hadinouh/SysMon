# Security policy

Security fixes target the latest SysMon release. Older versions may require upgrading to receive a fix.

## Reporting a vulnerability privately

Use GitHub's **Report a vulnerability** action under this repository's Security
tab when available:
https://github.com/Hadinouh/SysMon/security/advisories/new

Do not post exploit details, credentials or private logs in public issues. If
private reporting is not available, open an issue asking the maintainer to
enable a private contact channel, without disclosing the vulnerability itself.

Include the affected version, Windows version, a description of the impact and
minimal reproduction steps. Redact personal details from any attachments. There
is no guaranteed response time or bug bounty.

SysMon can run elevated, access hardware sensors, terminate processes, modify
startup entries and launch Windows tools. Reports involving privilege boundaries,
unsafe file loading, command execution or the sensor helper are especially useful.

Maintainers must enable GitHub private vulnerability reporting in repository
settings before advertising that channel as available.

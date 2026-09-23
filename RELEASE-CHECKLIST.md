# Final v1.0 publication

- [x] Document the application PNG artwork as ChatGPT-generated, based on maintainer-provided provenance, in ASSET-NOTICES.md.
- Enable private vulnerability reporting in GitHub repository settings and verify
  the link in SECURITY.md while signed in as a potential reporter.
- Publish the updated source, including the vendored LibreHardwareMonitor source,
  LICENSE and notices. Keep compiled binaries in GitHub Releases. Untracking an
  executable does not remove it from earlier Git history or existing releases.
- Build using `./package-release.ps1 -Candidate rc7`. The candidate remains unsigned.
- Complete the outstanding physical multi-monitor/DPI checks in V1.0-VERIFICATION.md.
- For a signed final release, obtain an Authenticode certificate/signing service.
  Sign the built SysMon.exe and SysMonSensors.exe using that service's documented
  SHA-256 signing and timestamp process, then verify their Authenticode signatures.
  Do not commit certificates, private keys or passwords. Regenerate SHA256SUMS.txt
  and the ZIP after signing; a signature changes the executable's hash.
- Publish the portable ZIP and its source material together. Replace older public
  candidates containing private debug paths if appropriate; local cleanup does
  not retract files already downloaded by other people.
- Do not label the candidate signed or final until those checks are complete.

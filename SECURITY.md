# Security policy

## Reporting

Please report vulnerabilities through GitHub's private vulnerability reporting feature. Do not include device passwords, SDK binaries, firmware images, packet captures containing credentials, or customer network details in a public issue.

## Operational scope

This tool sends local-network discovery traffic and can change the address of a selected device when a separately supplied vendor SDK is configured. Run it only on networks and devices you are authorized to administer.

The application does not persist passwords. A password is passed to the SDK for the selected operation, cleared from its temporary buffer, and never written to logs by this project. A vendor SDK may have its own logging behavior; review its documentation and runtime directory before using production credentials.

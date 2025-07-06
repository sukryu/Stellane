# Stellane Quickstart Guide

Welcome to **Stellane**, the ultimate C++20/23/26 backend framework for game servers and real-time applications! This guide helps you get started with Stellane in minutes, from installing the CLI to running your first server. Stellane delivers **blazing-fast performance**, **exceptional developer experience (DX)**, and **ironclad security** with zero compilation time.

---

## Table of Contents
1. [Prerequisites](#prerequisites)
2. [Install Stellane CLI](#install-stellane-cli)
3. [Create Your First Project](#create-your-first-project)
4. [Configure Your Project](#configure-your-project)
5. [Run Your Server](#run-your-server)
6. [Test Your API](#test-your-api)
7. [Troubleshooting](#troubleshooting)
8. [Next Steps](#next-steps)

---

## Prerequisites

Before starting, ensure you have:
- **Python 3.8+**: Required for the Stellane CLI.
- **C++ Compiler**: MSVC (Windows), GCC/Clang (Linux), or Clang (macOS) for custom builds (optional, as templates are prebuilt).
- **Dependencies** (optional, depending on template):
  - `libpqxx`: For PostgreSQL (Zenix++ ORM).
  - `openssl`: For TLS support.
- **curl**: For testing APIs (optional).

Install prerequisites:
- **Ubuntu/Debian**:
  ```bash
  sudo apt update
  sudo apt install python3 python3-pip build-essential libpq-dev libssl-dev curl
  ```
- **macOS (Homebrew)**:
  ```bash
  brew install python3 libpq openssl curl
  ```
- **Windows**:
	◦	Install Python.
	◦	Install MSVC (Build Tools).
	◦	Install curl via winget or manually.

## Install Stellane CLI
Install the Stellane CLI using pip:
pip install stellane
Verify installation:
stellane --version
**Output: stellane, version 0.2.0**
Note: Ensure pip installs to a user directory (no sudo) to align with Stellane’s security model. Use pip install --user if needed.

## Create Your First Project
Create a new project using the stellane new command with a prebuilt template (e.g., authserver-rest-jwt for a REST API with JWT authentication):
stellane new my-game-server --template authserver-rest-jwt
This command:
	1	Downloads the authserver-rest-jwt template from the Stellane Template Registry.
	2	Verifies integrity using SHA256 (and optional GPG).
	3	Extracts the template to the my-game-server directory.
	4	Prompts for configuration variables (e.g., PORT, DATABASE_URL).
Example Interaction:
$ stellane new my-game-server --template authserver-rest-jwt
Fetching manifest.json... [OK]
Downloading authserver-rest-jwt@latest (1.0.1)... [OK]
Verifying checksum... [OK]
Enter the server port [8080]:
Enter the database connection URL []: postgres://user:pass@localhost:5432/db
Enter the JWT secret key [hidden]: my-secret-key
Project created at my-game-server/
Project Structure:
```
my-game-server/
├── .env
├── .stellane/
│   ├── project.json
├── bin/
│   ├── linux-x86_64/authserver
│   ├── windows-x86_64/authserver.exe
│   ├── macos-arm64/authserver
├── docs/
│   ├── QUICKSTART.md
├── config/
│   ├── default.toml
```
## Configure Your Project
The .env file (created with 0600 permissions) contains environment variables defined in stellane.template.toml. Edit it if needed:
PORT=8080
DATABASE_URL=postgres://user:pass@localhost:5432/db
JWT_SECRET=my-secret-key
Note: Ensure your PostgreSQL database is running and accessible if using a template like authserver-rest-jwt. For example:
psql -U user -d db

## Run Your Server
Navigate to your project directory and run the server using stellane run:
cd my-game-server
stellane run
This command:
	1	Loads .env variables.
	2	Executes the platform-specific binary (e.g., bin/linux-x86_64/authserver).
	3	Logs output to ~/.stellane/logs/my-game-server.log (0600 permissions).
Example Output:
Starting server on http://localhost:8080
Logs written to ~/.stellane/logs/my-game-server.log
Tip: Use --docker for containerized execution (if supported by the template):
stellane run --docker

## Test Your API
Test the server using curl or a tool like Postman. For the authserver-rest-jwt template, try:
curl http://localhost:8080/api/users
**Output: {"users": []}**
To test authentication (if applicable):
	1	Obtain a JWT token: curl -X POST http://localhost:8080/api/login -d '{"username": "user", "password": "pass"}'
	2	# Output: {"token": "eyJ..."}
	3	
	4	Use the token: curl -H "Authorization: Bearer eyJ..." http://localhost:8080/api/users
	5	

## Troubleshooting
	•	Command not found: Ensure stellane is in your PATH. Run pip install --user stellane and check ~/.local/bin (Linux/macOS) or %USERPROFILE%\AppData\Roaming\Python\Scripts (Windows).
	•	Checksum mismatch: Delete ~/.stellane/cache and retry (stellane new --template authserver-rest-jwt).
	•	Database connection failed: Verify DATABASE_URL in .env and ensure PostgreSQL is running.
	•	Logs: Check ~/.stellane/logs/my-game-server.log for errors.
For further help, see CLI Reference or join our community.

## Next Steps
Congratulations, you’ve launched your first Stellane server! 🚀 Explore more:
	•	Learn More: Check registry-template-spec.md for template details or manifest-json-spec.md for registry metadata.
	•	Customize: Edit src/ (if included) to modify the template or build from source (stellane.template.toml [scripts.build]).
	•	Contribute: Add your own templates or improve Stellane (CONTRIBUTING.md).
	•	Community: Join our GitHub Discussions or Discord.

## License
MIT Licens

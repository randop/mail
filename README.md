# Mail
Privacy-first and self-hosted email server for modern-era 2026 instead of legacy 1999

## Project

To install dependencies:

```bash
bun install
```

To run:

```bash
bun run src/index.ts
```

To build:
```bash
bun run build
```
or
```bash
bun build ./src/index.ts --outdir ./.dist --target=node --format=esm
```


### Develop

#### Generate localhost ssl certificate
> Non-interactive and 10 years expiration
```shell
openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -sha256 -days 3650 -nodes -subj "/C=XX/ST=StateName/L=CityName/O=CompanyName/OU=CompanySectionName/CN=CommonNameOrHostname"
```

#### Setup flatbuffers toolkit
```bash
sudo pacman -Syy flatbuffers
flatc --version
```

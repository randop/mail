# Mail
Privacy-first and self-hosted email server for modern-era 2026 instead of legacy 1999

## Project

To install dependencies:

```shell
bun install
```

To run:

```shell
bun run src/index.ts
```

To build:
```shell
bun run build
```
or
```shell
bun build ./src/index.ts --outdir ./.dist --target=node --format=esm
```


### Develop

#### Generate localhost ssl certificate
> Non-interactive and 10 years expiration
```shell
openssl req -x509 -newkey rsa:4096 \
  -keyout key.pem -out cert.pem \
  -sha256 -days 3650 -nodes \
  -subj "/C=PH/ST=State/L=City/O=Company/OU=Team/CN=localhost"
```

#### Setup flatbuffers toolkit
```shell
sudo pacman -Syy flatbuffers
flatc --version
```

---

>  Copyright © 2010 — 2026 Randolph Ledesma
>
> Licensed under the Apache License, Version 2.0 (the "License");
> you may not use this file except in compliance with the License.
> You may obtain a copy of the License at
>
>    http://www.apache.org/licenses/LICENSE-2.0
>
> Unless required by applicable law or agreed to in writing, software
> distributed under the License is distributed on an "AS IS" BASIS,
> WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
> See the License for the specific language governing permissions and
> limitations under the License.
>

Copyright © 2010 — 2026 [Randolph Ledesma](https://gitlab.com/randop).

Last updated on 2026-01-05T06:31:01.000Z

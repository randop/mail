import { createRequire } from "node:module";
import { existsSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
const dirname = fileURLToPath(new URL(".", import.meta.url));
const require = createRequire(import.meta.url);

interface PackageJson {
  name: string;
  version: string;
}

let pkg: PackageJson;

if (existsSync(join(dirname, "./package.json"))) {
  pkg = require("./package.json") as PackageJson;
} else {
  pkg = require("../../package.json") as PackageJson;
}

export const APP_VERSION = pkg.version;
export const APP_NAME = pkg.name;

export default pkg;

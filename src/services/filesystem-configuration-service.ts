import log from "@/helpers/log";
import { resolve } from "node:path";
import { readFileSync, writeFileSync, unlinkSync } from "node:fs";
import { FileSystemConfig } from "@/lib/config";
import { randomUUID } from "node:crypto";

export class FileSystemConfigurationService {
  constructor() {
    //void
  }

  private _check(config: FileSystemConfig): boolean {
    const isOK: boolean = true;
    const fileUUID: string = "." + crypto.randomUUID() + ".check";
    const filePath = resolve(config.directory, fileUUID);
    try {
      writeFileSync(filePath, fileUUID);
    } catch (error: unknown) {
      isOK = false;
    }
    try {
      unlinkSync(filePath);
    } catch (error: unknown) {
      isOK = false;
    }
    return isOK;
  }

  load(): FileSystemConfig {
    const configFile = readFileSync("/etc/mail/config.json", "utf-8");
    const config = JSON.parse(configFile) as FileSystemConfig;
    if (!this._check(config)) {
      // void
    }
    return config;
  }
}

export default FileSystemConfigurationService;
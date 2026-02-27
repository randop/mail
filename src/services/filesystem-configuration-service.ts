import log from "@/helpers/log";
import { resolve } from "node:path";
import { readFileSync, writeFileSync, unlinkSync } from "node:fs";
import { FileSystemConfig } from "@/lib/config";
import { randomUUID } from "node:crypto";
import { FailureResult, SuccessResult, type Result } from "@/lib/types";
import { AppErrorCode } from "@/lib/errors";
import { toAppError } from "@/helpers/error";

export class FileSystemConfigurationService {
  constructor() {
    //void
  }

  private _check(config: FileSystemConfig): Result<boolean> {
    try {
      readFileSync("/etc/mail/accounts.whitelist", "utf-8");
    } catch (err: unknown) {
      const error = toAppError(err);
      error.code = AppErrorCode.VALIDATION_ERROR;
      return FailureResult(error);
    }

    try {
      readFileSync("/etc/mail/accounts.blacklist", "utf-8");
    } catch (err: unknown) {
      const error = toAppError(err);
      error.code = AppErrorCode.VALIDATION_ERROR;
      return FailureResult(error);
    }

    try {
      readFileSync("/etc/mail/ips.blacklist", "utf-8");
    } catch (err: unknown) {
      const error = toAppError(err);
      error.code = AppErrorCode.VALIDATION_ERROR;
      return FailureResult(error);
    }

    /** Check-test write on the email directory **/
    const fileUUID: string = "." + randomUUID() + ".check";
    const filePath = resolve(config.directory, fileUUID);
    try {
      writeFileSync(filePath, fileUUID);
    } catch (err: unknown) {
      const error = toAppError(err);
      error.code = AppErrorCode.VALIDATION_ERROR;
      return FailureResult(error);
    }

    try {
      unlinkSync(filePath);
    } catch (err: unknown) {
      const error = toAppError(err);
      error.code = AppErrorCode.VALIDATION_ERROR;
      return FailureResult(error);
    }

    return SuccessResult(true);
  }

  load(): FileSystemConfig {
    const configFile = readFileSync("/etc/mail/config.json", "utf-8");
    const config = JSON.parse(configFile) as FileSystemConfig;
    const checkResult = this._check(config);
    if (!checkResult.ok) {
      throw checkResult.error;
    }
    return config;
  }

  emailAccounts(): Set<string> {
    const accounts = new Set<string>();
    const accountsFile = readFileSync("/etc/mail/accounts.whitelist", "utf-8");
    const accountsParsed = accountsFile.split(/\r?\n/);
    for (const line of accountsParsed) {
      const account = line.trim();
      if (account.length > 0) {
        accounts.add(account);
      }
    }
    return accounts;
  }

  blacklistAccounts(): Set<string> {
    const blockedAccounts = new Set<string>();
    const blockedFile = readFileSync("/etc/mail/accounts.blacklist", "utf-8");
    const blockedParsed = blockedFile.split(/\r?\n/);
    for (const line of blockedParsed) {
      const blocked = line.trim();
      if (blocked.length > 0) {
        blockedAccounts.add(blocked);
      }
    }
    return blockedAccounts;
  }

  blacklistIps(): Set<string> {
    const blockedIPs = new Set<string>();
    const blockedIpFile = readFileSync("/etc/mail/ips.blacklist", "utf-8");
    const blockedIpParsed = blockedIpFile.split(/\r?\n/);
    for (const line of blockedIpParsed) {
      const blocked = line.trim();
      if (blocked.length > 0) {
        blockedIPs.add(blocked);
      }
    }
    return blockedIPs;
  }
}

export default FileSystemConfigurationService;

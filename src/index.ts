import CacheService from "@/services/cache-service";
import type { NullableString, SessionHostCheckResult } from "@/lib/types";
import { SMTPServer } from "smtp-server";
import { checkSessionHostname, verifyEmailDomain } from "@/helpers/util";
import { v7 as uuidv7 } from "uuid";
import { readFileSync } from "node:fs";
import { writeFile } from "node:fs/promises";
import {
  SMTP_551_USERNOTLOCALPLEASETTRY,
  SMTP_552_EXCEEDEDSTORAGEALLOCATION,
  SMTP_553_MAILBOXNAMENOTALLOWED,
  SMTP_554_TRANSACTIONFAILED,
  SMTP_221_SERVICECLOSINGTRANSMISSIONCHANNEL,
} from "./lib/response-codes";
import type { SessionReputation } from "@/lib/reputation";
import { ReputationStatus, ReputationDecision } from "@/lib/reputation";

import log from "@/helpers/log";

const EXIT_SUCCESS: number = 0;

const cache = new CacheService();
await cache.connect();

let useDirectLocalhost: boolean = false;
if (typeof process.env.EMAIL_SERVICE_DIRECT !== null) {
  log.warn("!!!Using DIRECT email service for localhost!!!");
  useDirectLocalhost = true;
}

let verifyAllowLocalhost: boolean = false;
let verifyServerName: boolean = false;
let verifyMailFrom: boolean = false;
let verifyMailTo: boolean = false;
let useProxy: boolean = true;
let disableReverseLookup: boolean = true;
let allowInsecureAuth: boolean = true;
let logger: boolean = true;
let useSecure: boolean = false;

if (useDirectLocalhost) {
  useSecure = false;
  useProxy = false;
}

const emailAccounts = new Set<string>();
const accountsFile = readFileSync("/etc/mail/accounts.whitelist", "utf-8");
const accountsParsed = accountsFile.split(/\r?\n/);
for (const line of accountsParsed) {
  const account = line.trim();
  if (account.length > 0) {
    emailAccounts.add(account);
  }
}

const blockedAccounts = new Set<string>();
const blockedFile = readFileSync("/etc/mail/accounts.blacklist", "utf-8");
const blockedParsed = blockedFile.split(/\r?\n/);
for (const line of blockedParsed) {
  const blocked = line.trim();
  if (blocked.length > 0) {
    blockedAccounts.add(blocked);
  }
}

const blockedIPs = new Set<string>();
const blockedIpFile = readFileSync("/etc/mail/ips.blacklist", "utf-8");
const blockedIpParsed = blockedIpFile.split(/\r?\n/);
for (const line of blockedIpParsed) {
  const blocked = line.trim();
  if (blocked.length > 0) {
    blockedIPs.add(blocked);
  }
}

const emailDirectory: string = "/emails/new/";
const certFile: string = "/emails/certs/cert.pem";
const sslKeyFile: string = "/emails/certs/key.pem";

const options: any = {
  key: readFileSync(sslKeyFile),
  cert: readFileSync(certFile),
  banner: "Ready for emails",
  logger,
  secure: useSecure,
  allowInsecureAuth,
  name: "EmailServerSandbox",
  size: 5242880, // 5MB
  disabledCommands: [],
  authMethods: ["PLAIN", "LOGIN", "CRAM-MD5", "XOAUTH2"],
  authOptional: true,
  maxClients: 30,
  useProxy,
  useXClient: true,
  hidePIPELINING: true,
  useXForward: true,
  disableReverseLookup,
  socketTimeout: 30000,
  closeTimeout: 15000,
  onAuth(auth, session, callback) {
    // auth.method → 'PLAIN', 'LOGIN', 'XOAUTH2', or 'CRAM-MD5'
    // Return `callback(err)` to reject, `cal  lback(null, response)` to accept
    callback(null, { user: auth.username });
  },
  onSecure(socket, session, callback) {
    if (verifyServerName && session.servername !== "mail.quizbin.com") {
      return callback(new Error("SNI mismatch"));
    }
    callback();
  },
  async onConnect(session, callback) {
    if (verifyAllowLocalhost && session.remoteAddress === "127.0.0.1") {
      return callback(new Error("Connections from localhost are forbidden"));
    }
    const theIP = session.remoteAddress;
    if (blockedIPs.has(theIP)) {
      log.error(`Blocked: ${theIP}`);
      return callback(
        Object.assign(new Error("Connection forbidden"), {
          responseCode: SMTP_221_SERVICECLOSINGTRANSMISSIONCHANNEL,
        }),
      );
    }
    let reputation: NullableString = await cache.get(`reputation_${theIP}`);
    if (reputation !== null && reputation == ReputationStatus.BANNED) {
      log.error(`Banned: ${theIP}`);
      return callback(
        Object.assign(new Error("Transaction connection failure"), {
          responseCode: SMTP_554_TRANSACTIONFAILED,
        }),
      );
    }
    const sr: SessionReputation = {
      status: ReputationStatus.UNKNOWN,
      decision: ReputationDecision.UNKNOWN,
    };
    session.reputation = sr;
    callback(); // accept
  },
  onClose(session) {
    //console.log(`Connection from ${session.remoteAddress} closed`);
  },
  onMailFrom(address, session, callback) {
    if (verifyMailFrom && !address.address.endsWith("@quizbin.com")) {
      return callback(
        Object.assign(new Error("Relay denied"), {
          responseCode: SMTP_553_MAILBOXNAMENOTALLOWED,
        }),
      );
    }
    callback();
  },
  onRcptTo(address, session, callback) {
    const checkResult: SessionHostCheckResult = checkSessionHostname(session);
    if (!checkResult.ok) {
      return callback(
        Object.assign(new Error(checkResult.error.message), {
          responseCode: SMTP_554_TRANSACTIONFAILED,
        }),
      );
    }

    if (verifyMailTo && !address.address.endsWith("@quizbin.com")) {
      return callback(
        Object.assign(new Error(`User ${address.address} is unknown`), {
          responseCode: SMTP_551_USERNOTLOCALPLEASETTRY,
        }),
      );
    }

    callback();
  },
  onData(stream, session, callback) {
    let data: string = "";
    stream.on("data", (chunk: Buffer) => {
      data += chunk.toString("utf8");
    });
    stream.on("end", async () => {
      if (stream.sizeExceeded) {
        const err = Object.assign(new Error("Message is too large"), {
          responseCode: SMTP_552_EXCEEDEDSTORAGEALLOCATION,
        });
        return callback(err);
      } else {
        const uuid: string = uuidv7();
        let emailFile = `${emailDirectory}${uuid}`;
        try {
          await writeFile(emailFile, data, "utf8");
        } catch (err) {
          log.error(`Error writing email file: ${emailFile}`);
        }
      }
      callback(null, "OK");
    });
  },
};

log.info(`Using email directory: ${emailDirectory}`);

const server = new SMTPServer(options);

const port: number = 2525;
const host: string = "0.0.0.0";

server.listen(port, host, () => {
  if (!logger) {
    log.info(`SMTP server ${host} listening on port ${port}`);
  }
});

server.on("error", (err) => {
  log.error("SMTP Server error:", err.message);
});

// Graceful shutdown
process.on("SIGINT", () => {
  log.info("Shutting down SMTP server...");
  cache.close();
  server.close(() => {
    process.exit(EXIT_SUCCESS);
  });
});
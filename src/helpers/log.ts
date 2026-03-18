/***
 * Console logging helper
 */

const ANSI = {
  red: "\x1b[31m",
  yellow: "\x1b[33m",
  green: "\x1b[32m",
  blue: "\x1b[94m",
  black: "\x1b[30m",
  reset: "\x1b[0m",
  bold: "\x1b[1m",
} as const;

type Level = "E" | "W" | "D" | "T";

const LEVELS: Record = {
  E: ANSI.bold + ANSI.red,
  W: ANSI.yellow,
  D: ANSI.green,
  T: ANSI.blue,
};

interface Logger {
  info(...args: any[]): void;
  error(...args: any[]): void;
  warn(...args: any[]): void;
  debug(...args: any[]): void;
  trace(...args: any[]): void;
  exception(...args: any[]): void;
}

// Helper to format logging with timestamp [YYYY-MM-DD hh:mm:ss]
function formatTimestamp(): string {
  const date = new Date();
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, "0");
  const day = String(date.getDate()).padStart(2, "0");
  const hours = String(date.getHours()).padStart(2, "0");
  const minutes = String(date.getMinutes()).padStart(2, "0");
  const seconds = String(date.getSeconds()).padStart(2, "0");
  // JS only has millisecond precision, so the last 3 digits are padded with 000 to mimic the 6-digit microsecond
  const microseconds = String(date.getMilliseconds()).padStart(3, "0") + "000";
  const offsetMinutes = date.getTimezoneOffset();
  const sign = offsetMinutes <= 0 ? "+" : "-";
  const absOffset = Math.abs(offsetMinutes);
  const offsetHours = String(Math.floor(absOffset / 60)).padStart(2, "0");
  const offsetSecs = String(absOffset % 60).padStart(2, "0");

  return `[${year}-${month}-${day}T${hours}:${minutes}:${seconds}.${microseconds}${sign}${offsetHours}:${offsetSecs}]`;
}

const errorColor = LEVELS["E"];
const warnColor = LEVELS["W"];
const debugColor = LEVELS["D"];
const traceColor = LEVELS["T"];

// The logger proxy
export const log: Logger = {
  info: (...args: any[]) => {
    console.log(formatTimestamp(), "[I]", ...args);
  },
  error: (...args: any[]) => {
    console.error(
      `${errorColor}${formatTimestamp()}`,
      "[E]",
      ...args,
      `${ANSI.reset}`,
    );
  },
  warn: (...args: any[]) => {
    console.warn(
      `${warnColor}${formatTimestamp()}`,
      `[W]${ANSI.reset}`,
      ...args,
    );
  },
  debug: (...args: any[]) => {
    console.debug(
      `${debugColor}${formatTimestamp()}`,
      "[D]",
      ...args,
      `${ANSI.reset}`,
    );
  },
  trace: (...args: any[]) => {
    console.trace(
      `${traceColor}${formatTimestamp()}`,
      `[T]${ANSI.reset}`,
      ...args,
    );
  },
  exception: (...args: any[]) => {
    console.trace(
      `${errorColor}${formatTimestamp()}`,
      `[E]${ANSI.reset}`,
      ...args,
    );
  },
};

export default log;

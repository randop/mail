/***
 * Console logging helper
 */

interface Logger {
  info(...args: any[]): void;
  error(...args: any[]): void;
  warn(...args: any[]): void;
}

// Helper to format logging with timestamp [YYYY-MM-DD hh:mm:ss]
function formatTimestamp(): string {
  const date = new Date();
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  const seconds = String(date.getSeconds()).padStart(2, '0');

  return `[${year}-${month}-${day} ${hours}:${minutes}:${seconds}]`;
}

// The logger proxy
export const log: Logger = {
  info: (...args: any[]) => {
    console.log(formatTimestamp(), 'INFO', ...args);
  },
  error: (...args: any[]) => {
    console.error(formatTimestamp(), 'ERROR', ...args);
  },
  warn: (...args: any[]) => {
    console.warn(formatTimestamp(), 'WARNING', ...args);
  },
};

export default log;

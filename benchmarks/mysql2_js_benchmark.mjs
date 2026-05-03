#!/usr/bin/env node
import { createRequire } from 'node:module';

const require = createRequire(`${process.cwd()}/mysql2-js-benchmark.cjs`);
let mysql;
try {
  mysql = require('mysql2/promise');
} catch (error) {
  console.error('Cannot load mysql2/promise. Run from a directory where npm package mysql2 is installed.');
  console.error('Example: npm install mysql2@3.22.3');
  throw error;
}

function env(name) {
  return process.env[name];
}

function envInt(name, fallback) {
  const value = env(name);
  return value ? Number.parseInt(value, 10) : fallback;
}

function optionsFromEnv() {
  return {
    host: env('MYSQL2_TEST_HOST') ?? '127.0.0.1',
    port: envInt('MYSQL2_TEST_PORT', 3306),
    user: env('MYSQL2_TEST_USER') ?? 'root',
    password: env('MYSQL2_TEST_PASSWORD') ?? '',
    database: env('MYSQL2_TEST_DATABASE') ?? undefined,
    charset: 'utf8mb4',
    iterations: envInt('MYSQL2_BENCHMARK_ITERATIONS', 1000),
    fetchRows: envInt('MYSQL2_BENCHMARK_ROWS', 1000),
    fetchRepeats: envInt('MYSQL2_BENCHMARK_FETCH_REPEATS', 50),
  };
}

async function measureMs(fn) {
  const start = process.hrtime.bigint();
  await fn();
  const end = process.hrtime.bigint();
  return Number(end - start) / 1_000_000;
}

function printResult(client, workload, iterations, totalMs) {
  const opsPerSec = totalMs > 0 ? iterations * 1000 / totalMs : 0;
  console.log(`${client},${workload},${iterations},${totalMs.toFixed(3)},${opsPerSec.toFixed(3)}`);
}

async function main() {
  const options = optionsFromEnv();
  const connection = await mysql.createConnection({
    host: options.host,
    port: options.port,
    user: options.user,
    password: options.password,
    database: options.database,
    charset: options.charset,
    connectTimeout: 5000,
  });

  try {
    await connection.ping();
    console.log('client,workload,iterations,total_ms,ops_per_sec');

    const textMs = await measureMs(async () => {
      for (let i = 0; i < options.iterations; i += 1) {
        const [rows] = await connection.query('SELECT 1 AS one');
        if (rows.length !== 1 || rows[0].one !== 1) {
          throw new Error('mysql2_js text query returned wrong result');
        }
      }
    });
    printResult('mysql2_js', 'text_select_1', options.iterations, textMs);

    const statement = await connection.prepare('SELECT ? + ? AS sum_value');
    try {
      const preparedMs = await measureMs(async () => {
        for (let i = 0; i < options.iterations; i += 1) {
          const [rows] = await statement.execute([i, 1]);
          if (rows.length !== 1 || rows[0].sum_value !== i + 1) {
            throw new Error('mysql2_js prepared query returned wrong result');
          }
        }
      });
      printResult('mysql2_js', 'prepared_add', options.iterations, preparedMs);
    } finally {
      await statement.close();
    }

    const fetchSql = `WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < ${options.fetchRows}) SELECT n FROM seq`;
    const fetchMs = await measureMs(async () => {
      for (let repeat = 0; repeat < options.fetchRepeats; repeat += 1) {
        const [rows] = await connection.query(fetchSql);
        if (rows.length !== options.fetchRows) {
          throw new Error(`mysql2_js fetch row count mismatch: ${rows.length}`);
        }
      }
    });
    printResult('mysql2_js', 'fetch_rows', options.fetchRows * options.fetchRepeats, fetchMs);

    const materializeMs = await measureMs(async () => {
      for (let repeat = 0; repeat < options.fetchRepeats; repeat += 1) {
        const [rows] = await connection.query(fetchSql);
        let sum = 0;
        const values = new Array(rows.length);
        for (let i = 0; i < rows.length; i += 1) {
          values[i] = rows[i].n;
          sum += values[i];
        }
        if (values.length !== options.fetchRows || sum <= 0) {
          throw new Error(`mysql2_js materialized row count mismatch: ${values.length}`);
        }
      }
    });
    printResult('mysql2_js', 'fetch_rows_materialized', options.fetchRows * options.fetchRepeats, materializeMs);
  } finally {
    await connection.end();
  }
}

main().catch((error) => {
  console.error(error?.stack ?? String(error));
  process.exitCode = 1;
});

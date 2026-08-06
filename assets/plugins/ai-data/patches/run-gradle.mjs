#!/usr/bin/env node
/**
 * PP ai-data patch — uses shell env (pp env profile) when .envrc is absent.
 */
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
	awsCredsFingerprint,
	formatLoadedEnvReport,
	githubPackagesCredsPresent,
	loadEnvrc,
} from './load-envrc.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const gradlew = process.platform === 'win32' ? 'gradlew.bat' : './gradlew';
const args = process.argv.slice(2);
if (!args.length) {
	console.error('Usage: node scripts/run-gradle.mjs <gradle-args…>');
	process.exit(1);
}

function syncEnvAliases() {
	if (process.env.GH_USER && !process.env.GITHUB_USER) {
		process.env.GITHUB_USER = process.env.GH_USER;
	}
	if (process.env.GH_TOKEN && !process.env.GITHUB_TOKEN) {
		process.env.GITHUB_TOKEN = process.env.GH_TOKEN;
	}
	if (process.env.AWS_REGION_KEY_PROPERTY && !process.env.AWS_REGION) {
		process.env.AWS_REGION = process.env.AWS_REGION_KEY_PROPERTY;
	}
}

const loaded = loadEnvrc(root, { overwrite: true });
if (loaded.missing) {
	syncEnvAliases();
	if (!githubPackagesCredsPresent()) {
		console.error('Missing .envrc and GH_USER/GH_TOKEN not in environment');
		console.error('Run: pp env edit ai-data --global');
		process.exit(1);
	}
	console.log('Using environment vars (PP profile ai-data — no .envrc in repo)');
} else {
	console.log(formatLoadedEnvReport(loaded));
}

if (!githubPackagesCredsPresent()) {
	console.error('GH_USER / GH_TOKEN missing or still placeholders');
	process.exit(1);
}

const aws = awsCredsFingerprint();
console.log('AWS env passed to Gradle/JVM:');
for (const [k, v] of Object.entries(aws)) {
	console.log(`  ${k}=${v}`);
}
if (aws.AWS_ACCESS_KEY_ID === '(empty)' || aws.AWS_SECRET_ACCESS_KEY === '(empty)') {
	console.warn('WARNING: AWS keys empty — Bedrock chat will fail with auth errors.');
}

const result = spawnSync(gradlew, args, {
	cwd: root,
	stdio: 'inherit',
	shell: process.platform === 'win32',
	env: process.env,
});
process.exit(result.status ?? 1);

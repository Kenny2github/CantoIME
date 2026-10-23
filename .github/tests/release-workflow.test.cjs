// Run with: node --test .github/tests/release-workflow.test.cjs
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const workflow = readFileSync(join(__dirname, '../workflows/release.yml'), 'utf8');
const script = workflow.split('          script: |\n')[1].split('\n\n')[0]
  .split('\n').map(line => line.slice(12)).join('\n');
const execute = new (Object.getPrototypeOf(async function () {}).constructor)(
  'github', 'context', 'core', 'setTimeout', script);

async function selectBuild(batches) {
  let calls = 0;
  const outputs = {};
  const github = {
    rest: { actions: { listWorkflowRuns: 'listWorkflowRuns' } },
    paginate: async (method, params) => {
      assert.equal(method, 'listWorkflowRuns');
      assert.equal(params.workflow_id, 'build.yml');
      assert.equal(params.head_sha, 'tagged-commit');
      return batches[Math.min(calls++, batches.length - 1)];
    },
  };
  await execute(github, { repo: { owner: 'owner', repo: 'repo' }, sha: 'tagged-commit' },
    { setOutput: (key, value) => { outputs[key] = value; }, info: () => {} },
    callback => callback());
  return { outputs, calls };
}

const run = (id, status, conclusion, sha = 'tagged-commit') =>
  ({ id, status, conclusion, head_sha: sha });

test('selects the tagged commit, not a successful build for another commit', async () => {
  const result = await selectBuild([[
    run(9, 'completed', 'success', 'other-commit'),
    run(8, 'completed', 'failure'),
    run(7, 'completed', 'success'),
  ]]);
  assert.equal(result.outputs['run-id'], 7);
});

test('waits for a missing or running build to finish', async () => {
  const result = await selectBuild([[], [run(1, 'in_progress', null)],
    [run(1, 'completed', 'success')]]);
  assert.equal(result.calls, 3);
  assert.equal(result.outputs['run-id'], 1);
});

test('fails if all matching builds failed', async () => {
  await assert.rejects(selectBuild([[run(1, 'completed', 'failure')]]),
    /No successful Build run/);
});

test('times out without using another commit', async () => {
  await assert.rejects(selectBuild([[run(9, 'completed', 'success', 'other-commit')]]),
    /Timed out waiting/);
});

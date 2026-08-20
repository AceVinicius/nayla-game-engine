export const meta = {
  name: 'bugfix',
  description: 'Fix a bug with root-cause analysis and regression coverage',
  phases: [
    { title: 'Analysis' },
    { title: 'Reproduction' },
    { title: 'Root Cause' },
    { title: 'Fix' },
    { title: 'Validation' },
  ],
}

const task = await agent('Read and understand the bug report/task. Identify the affected subsystem.', {
  label: 'bugfix:analysis',
  phase: 'Analysis',
})

const repro = await agent(`Write the smallest test that reproduces this bug: ${task}. Run it to confirm failure.`, {
  label: 'bugfix:repro',
  phase: 'Reproduction',
})

const rootCause = await agent(`Trace the root cause of this bug: ${repro}. Load affected subsystem rules from .claude/rules/. Report root cause.`, {
  label: 'bugfix:root-cause',
  phase: 'Root Cause',
})

const fix = await agent(`Fix the root cause: ${rootCause}. Make the minimal fix without refactoring unrelated code.`, {
  label: 'bugfix:fix',
  phase: 'Fix',
})

const regressionTest = await agent(`Add regression coverage for this bug to prevent re-occurrence: ${rootCause}`, {
  label: 'bugfix:regression',
  phase: 'Fix',
})

const validate = await agent('Run targeted tests on the fixed code. Run broader tests if required by the task. Report pass/fail.', {
  label: 'bugfix:validate',
  phase: 'Validation',
})

return { task, rootCause, fix, regressionTest, validate }

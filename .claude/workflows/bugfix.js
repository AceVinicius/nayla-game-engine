export const meta = {
  name: 'bugfix',
  description: 'Fix a bug with root-cause analysis and regression coverage',
  phases: [{ title: 'Analysis' }, { title: 'Repro' }, { title: 'Root Cause' }, { title: 'Fix' }, { title: 'Validate' }],
}

const task = await agent('Extract: broken, subsystem, observed vs expected.', { label: 'bugfix:analysis', phase: 'Analysis', schema: { broken: 'string', subsystem: 'string', observed: 'string', expected: 'string' } })
log(`Bug: ${task.broken} (${task.subsystem})`)

const repro = await agent(`Write smallest test: ${JSON.stringify(task)}. Run, confirm fails, report test + failure.`, { label: 'bugfix:repro', phase: 'Repro' })

const rootCause = await agent(`Trace: ${repro}. Load rules/subsystem. Find root (not symptom). Report: file:line, cause, why.`, { label: 'bugfix:cause', phase: 'Root Cause', schema: { file: 'string', line: 'number', cause: 'string', why: 'string' } })

const fix = await agent(`Fix: ${JSON.stringify(rootCause)}. Minimal (no refactor). Apply, verify repro passes.`, { label: 'bugfix:fix', phase: 'Fix' })

const regress = await agent(`Regression: ${JSON.stringify(rootCause)}. Write test catching bug, add to suite.`, { label: 'bugfix:regress', phase: 'Fix' })

const validate = await agent('Run tests. Report: passed, failed, failures (concise).', { label: 'bugfix:validate', phase: 'Validate', schema: { passed: 'number', failed: 'number', failures: 'array' } })

return { task, repro, rootCause, fix, regress, validate }

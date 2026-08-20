export const meta = {
  name: 'feature',
  description: 'Implement a feature from a well-defined task',
  phases: [{ title: 'Understand' }, { title: 'Implement' }, { title: 'Validate' }, { title: 'Review' }],
}

const task = await agent('Extract task objective, requirements, acceptance criteria.', { label: 'feature:task', phase: 'Understand', schema: { objective: 'string', requirements: 'array', criteria: 'array' } })
log(`Feature: ${task.objective}`)

const impl = await agent(`Implement per task: ${JSON.stringify(task)}. Inspect source, smallest change, accept criteria only. Load task-named files.`, { label: 'feature:impl', phase: 'Implement' })

const tests = await agent('Run targeted tests. Report: passed, failed, failures (concise).', { label: 'feature:test', phase: 'Validate', schema: { passed: 'number', failed: 'number', failures: 'array' } })

if (tests.failed > 0) {
  log(`${tests.failed} failed — fixing`)
  const fixes = await agent(`Fix failures: ${tests.failures.join('; ')}. Rerun, report pass/fail.`, { label: 'feature:fix', phase: 'Validate', schema: { passed: 'number', failed: 'number' } })
  tests.passed = fixes.passed; tests.failed = fixes.failed
}

const review = await agent(`Audit: (1) task done? ${task.criteria}; (2) arch sound? (3) scope tight? (4) tests ok? Report: ready/blockers.`, { label: 'feature:review', phase: 'Review', schema: { ready: 'boolean', blockers: 'array' } })

return { task, impl, tests, review }

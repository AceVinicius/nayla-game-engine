export const meta = {
  name: 'feature',
  description: 'Implement a feature from a well-defined task',
  phases: [
    { title: 'Task Review' },
    { title: 'Implementation' },
    { title: 'Validation' },
    { title: 'Review' },
  ],
}

const task = await agent('Read and summarize the assigned task, including context, requirements, and acceptance criteria', {
  label: 'feature:task-review',
  phase: 'Task Review',
})

const impl = await agent(`Implement the feature according to the task contract: ${task}. Inspect referenced source before changing. Make the smallest compliant change.`, {
  label: 'feature:implement',
  phase: 'Implementation',
})

const tests = await agent('Run targeted tests for the implemented feature and report results', {
  label: 'feature:test',
  phase: 'Validation',
})

if (tests.includes('fail') || tests.includes('error')) {
  log('Tests failed or errored — fixing')
  const fixes = await agent('Fix test failures and re-run', {
    label: 'feature:fix',
    phase: 'Validation',
  })
}

const review = await agent('Review the changes for correctness, scope, and test coverage. Report any blockers or assumptions.', {
  label: 'feature:review',
  phase: 'Review',
})

return { task, impl, tests, review }

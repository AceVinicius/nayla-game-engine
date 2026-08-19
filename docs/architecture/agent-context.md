# Agent Context Contract

Effective context:

```text
Global rules
+
Agent instructions
+
Task
+
Scoped rules
+
Referenced specification
+
Relevant source
```

Avoid loading all agents, rules, specs, or the whole repository.

If an agent needs a document, the task should name it in `Context.Required`.

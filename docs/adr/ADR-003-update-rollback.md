# ADR-003: Update and Rollback Strategy

## Status
Accepted

## Context
Sentinel OS must support safe updates of core components and extensions without data loss or system downtime.
Updates may introduce regressions, and rollback must be fast and reliable.

We need a strategy that:
- Supports atomic updates
- Minimizes downtime
- Enables fast rollback
- Maintains backward compatibility for data formats

## Decision
Adopt a **versioned module snapshot model** with atomic rollback capabilities.

### Update Model

#### Version Semantics
Use semantic versioning (MAJOR.MINOR.PATCH):
- **MAJOR**: Breaking interface changes; requires extension recompilation
- **MINOR**: New capabilities; backward compatible
- **PATCH**: Bug fixes; transparent

#### Update Procedure
1. New version published to `$SENTINEL_HOME/versions/{component}/{version}/`
2. Manifest file describes: interfaces, capabilities, dependencies, rollback instructions
3. Active version symlinked at `$SENTINEL_HOME/{component}/current/`
4. Update applied by: atomic symlink swap + in-process bridge
5. If update fails: symlink reverted to previous version; in-flight operations complete

#### Rollback Mechanism
- Maintain metadata file: `{component}/.rollback_history` with last 3 versions
- Rollback by: atomic symlink reversion + optional process restart
- Data migrations run forward and backward (version A → B → A must succeed)

### Safety Guarantees
1. **Atomic Metadata Updates**: Use write-ahead logging for version metadata
2. **In-Flight Operation Completion**: Wait for pending operations before version switch (default: 5 second timeout)
3. **Data Compatibility**: Version schema stored in data files; migration code must exist
4. **Verification**: Post-update smoke tests verify basic interface availability

### Storage Layout
```
$SENTINEL_HOME/
├── versions/
│   ├── core-runtime/
│   │   ├── 0.1.0/
│   │   │   ├── core_runtime.so
│   │   │   └── interfaces.h
│   │   └── 0.1.1/
│   │       ├── core_runtime.so
│   │       └── interfaces.h
│   └── policy-service/
│       ├── 0.1.0/
│       └── 0.1.1/
├── core-runtime -> versions/core-runtime/0.1.1/
├── policy-service -> versions/policy-service/0.1.1/
├── rollback_history.json (metadata)
└── data/
    ├── policies.db (versioned schema)
    └── extensions/ (with version metadata)
```

## Rationale
1. **Fast Rollback**: Symlink reversion is O(1)
2. **Minimal Downtime**: In-process bridge avoids process restart for minor updates
3. **Data Safety**: Versioned schemas prevent corruption
4. **Auditability**: Rollback history provides forensics
5. **Incremental Deployment**: Easy to canary new versions to subset of instances

## Consequences
- **Positive**: Safe, fast, and auditable updates with minimal operational complexity
- **Negative**: Requires forward/backward data migrations; symlink-based deployment not suitable for distributed systems
- **Mitigations**: Automated migration testing; canary deployments; monitoring alerts

## Implementation Schedule
1. Phase 1: Version layout and symlink management
2. Phase 2: Manifest format and validation
3. Phase 3: Update daemon with rollback capability
4. Phase 4: Data migration framework
5. Phase 5: Monitoring and alerting

## Future Considerations
- Consider content-addressed storage (e.g., Git-like DAG) for deduplication across versions
- Evaluate distributed update coordination for multi-instance deployments

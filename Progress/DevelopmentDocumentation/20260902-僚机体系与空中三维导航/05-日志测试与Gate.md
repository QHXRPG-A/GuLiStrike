---
schema: guli-progress/v1
id: DEV-20260902-001-D05
work_id: WORK-20260902-001
kind: development
role: detail
title: 僚机体系、空中三维导航与客户端校验转发 — 技术方案 · 日志、测试与 Gate
areas:
- wingman
- commander
- ship
- network
- combat
status: done
verification: passed
created: '2026-09-02'
updated: '2026-09-07'
summary: Display只记录创建/移除、Lease/Availability转换、死亡、导航不可用和QA总结；详细运动与网络样本写结构化文件
next_action: ''
relations:
  parent: DEV-20260902-001
status_note: 既定实现已收尾（Reward经济规则待玩法配置；源码Dedicated、完整Server Cook、20个真实Dedicated角色、2个拆分H4000角色及S9双10分钟运行已有通过记录；未追加的性能数值阈值不声明通过）
split_order: 5
split_segment_sha256: d78336d47129bc68dd75b078169854044985a420a1df1d1bb13f2b752d5688be
---

# 僚机体系、空中三维导航与客户端校验转发 — 技术方案 · 日志、测试与 Gate

<!-- guli-progress:split-content -->
## 9. 日志、字段与开发命令

### 9.1 日志类别

- `LogGuLiFlightNav`
- `LogGuLiWingman`
- `LogGuLiWingmanAI`
- `LogGuLiWingmanRelay`
- `LogGuLiBattleCombat`
- `LogGuLiWingmanNet`
- `LogGuLiWingmanQA`

Display只记录创建/移除、Lease/Availability转换、死亡、导航不可用和QA总结；详细运动与网络样本写结构化文件。

### 9.2 公共字段

`schema_version, campaign_id, run_id, pair_id, suite_run_role, event, map, net_mode, pid, utc_us, server_time_s, match_epoch, connection_generation, protocol, seed, acceptance_profile, catalog_version, catalog_hash, gate_id, gate_status, invariant_key, invariant_count`

身份/Lease：

`authority_mode, unit_kind, team, player_guid, owner_connection_id, simulation_group_id, group_generation, roster_revision, required_active_flight_mask, required_active_member_mask, flight_id, slot, wingman_id, entity_generation, submitted_authority_epoch, submitted_lease_id, active_authority_epoch, active_lease_id, pending_authority_epoch, pending_lease_id, availability_state`

Candidate/Accepted：

`requested_submission_mode, observed_high_rate_grant_revision, allowed_upload_rate_class, high_rate_grant_revision, high_rate_effective_client_sim_tick, high_rate_grant_expiry_ms, expected_submission_hz, capture_interval_client_ticks, client_sim_tick, capture_time_ms, frame_sequence, base_accepted_snapshot_sequence, accepted_snapshot_sequence, accepted_snapshot_hash, validated_payload_hash, candidate_payload_hash, observer_applied_payload_hash, batch_id, batch_kind, baseline_kind, baseline_revision, baseline_hash, frozen_roster_revision, frozen_required_flight_mask, frozen_required_member_mask_hash, included_flight_mask, committed_flight_mask, fragment_index, fragment_count, batch_payload_hash, batch_assembly_deadline_ms, atomic_commit, batch_staged_count, last_valid_candidate_time_ms, last_accepted_snapshot_time_ms, source_state_age_ms, target_state_age_ms, source_availability_state, target_availability_state, validator_trigger, validation_duration_us, validation_result, reject_reason, accepted_store_count, relay_count, server_generated_runtime_transform_count, server_wingman_motion_step_count, server_wingman_state_tree_tick_count, server_wingman_path_request_count, server_wingman_steering_step_count, server_wingman_flight_integration_step_count, server_ground_machine_motion_step_count, server_ground_machine_state_tree_tick_count, server_ground_machine_path_request_count, server_ground_machine_steering_step_count, server_ground_machine_movement_integration_step_count`

Lease Maintenance：

`watchdog_scan_id, watchdog_interval_ms, initial_candidate_deadline_ms, stale_deadline_ms, unavailable_deadline_ms, revoke_deadline_ms, offer_time_ms, offer_ready_deadline_ms, offer_ready_time_ms, takeover_ack_deadline_ms, revised_baseline_ack_deadline_ms, takeover_candidate_deadline_ms, takeover_overall_deadline_ms, initial_detection_lag_ms, stale_detection_lag_ms, unavailable_detection_lag_ms, revoke_detection_lag_ms, availability_before, availability_after, active_lease_transaction_state, pending_lease_transaction_state, backup_selection_attempt, preview_baseline_revision, transfer_baseline_revision, transfer_baseline_hash, frozen_roster_revision, transfer_commit_time_ms, transfer_baseline_ack_time_ms, baseline_revised_reason, revised_baseline_sent_time_ms, revised_baseline_ack_time_ms, resume_snapshot_sequence, resume_snapshot_hash, first_resume_proposal_id, first_takeover_candidate_id, resume_latency_ms, takeover_latency_ms`

运动：

`position, velocity, quaternion, speed, heading, heading_rate, bank, accel, desired_slot, radial_error, vertical_error, nearest_clearance, nav_checksum, corridor_revision, current_leaf, current_portal, obstacle_revision, remote_extrapolation_age_ms, remote_fade_duration_ms, remote_hidden_time_ms`

Combat：

`source_kind, source_id, source_generation, source_state_ref_type, source_accepted_snapshot_sequence, target_kind, target_id, target_generation, client_fire_tick, fire_sequence, submission_dedupe_key, dedupe_lookup_result, reservation_created, fire_sequence_high_water_before, fire_sequence_high_water_after, distance, range, los, shot_id, damage_event_id, damage, health_before, health_after, commit_count, death_event_id, reward_event_id, replenish_schedule_id, replenish_due_time_ms, deferred_replenish_due_count, pre_active_spawn_count, post_active_spawn_count, reject_reason`

性能：

`sample_count, minimum_sample_count, quantile_method, client_sim_ms, server_validator_ms, server_store_relay_ms, server_combat_ms, lease_watchdog_ms, commander_fixed_step_ms, queue_depth, queue_capacity, oldest_item_age_ms, memory_private_bytes, bytes, stream_tag, measurement_direction`

### 9.3 事件

`QA_RUN_BEGIN, PROFILE_SELECTED, PROCESS_READY, MAP_LOADED, FIXTURE_READY, NAV_READY, CARRIER_READY, GROUP_SPAWNED, ROSTER_COMMITTED, BASELINE_SENT, BASELINE_ACKED, LEASE_GRANTED, LEASE_RENEWED, LEASE_WATCHDOG_TICK, SIM_GROUP_STALE, SIM_GROUP_UNAVAILABLE, LEASE_REVOKED, LEASE_TRANSFER_OFFERED, LEASE_TRANSFER_READY, LEASE_TRANSFER_COMMIT, ACCEPTED_SNAPSHOT_FROZEN, RESUME_SNAPSHOT_SENT, RESUME_SNAPSHOT_ACKED, CLIENT_SIM_RESUME_BEGIN, FIRST_RESUME_PROPOSAL_ACCEPTED, CLIENT_SIM_RESUMED, HIGH_RATE_REQUESTED, HIGH_RATE_GRANTED, HIGH_RATE_EXPIRED, SUBMISSION_MODE_CHANGED, CANDIDATE_SUBMITTED, CANDIDATE_RECEIVED, CANDIDATE_ACCEPTED, CANDIDATE_REJECTED, ACCEPTED_SNAPSHOT_STORED, ACCEPTED_SNAPSHOT_RELAYED, ACCEPTED_SNAPSHOT_APPLIED, REBASE_SENT, REBASE_APPLIED, REMOTE_EXTRAPOLATION_ENDED, PATH_REQUESTED, PATH_RESULT, PATH_INVALIDATED, EMERGENCY_ENTER, EMERGENCY_EXIT, TARGET_ELIGIBLE, TARGET_ACQUIRED, FIRE_INTENT_SUBMITTED, FIRE_INTENT_RECEIVED, SHOT_ACCEPTED, SHOT_REJECTED, DAMAGE_APPLIED, DAMAGE_DEDUPED, HEALTH_ZERO, DEATH_COMMITTED, REPLENISH_SCHEDULED, REPLENISH_SPAWNED, SHOT_PRESENTED, HIT_PRESENTED, DEATH_PRESENTED, LATEJOIN_CONNECT_BEGIN, BOOTSTRAP_CUT_CREATED, BOOTSTRAP_COMMIT, LATEJOIN_READY, QA_CHECK, QA_RUN_END`

初始化/接管另必须记录`SIM_GROUP_INITIALIZING, FIRST_BOOTSTRAP_BATCH_ACCEPTED, INITIAL_CANDIDATE_DEADLINE_EXPIRED, FIRST_TAKEOVER_CANDIDATE_ACCEPTED`。
原子事务另必须记录`ATOMIC_BATCH_FRAGMENT_RECEIVED, ATOMIC_BATCH_ASSEMBLED, ATOMIC_BATCH_REJECTED, ATOMIC_BATCH_COMMITTED, ATOMIC_BATCH_DEADLINE_EXPIRED, SIM_BASELINE_REVISED, REVISED_BASELINE_ACKED, ACTIVE_LEASE_STALE_DURING_PENDING_OFFER, OFFER_READY_DEADLINE_EXPIRED, TAKEOVER_ACK_DEADLINE_EXPIRED, REVISED_BASELINE_ACK_DEADLINE_EXPIRED, TAKEOVER_CANDIDATE_DEADLINE_EXPIRED, TAKEOVER_OVERALL_DEADLINE_EXPIRED, BACKUP_SELECTION_RETRIED, SIM_GROUP_NO_OWNER`。
补充事务另必须记录`REPLENISH_DEFERRED_UNAVAILABLE, REPLENISH_RELEASED_AFTER_ACTIVE`。
Fire序列另必须记录`FIRE_DEDUPE_HIT, FIRE_SEQUENCE_RESERVED, FIRE_REJECTED_BEFORE_SEQUENCE_RESERVE`。
表现另必须记录`REMOTE_HIDDEN_AFTER_EXTRAPOLATION`。

### 9.4 不变量

以下`invariant_key`必须精确使用大写枚举：

`SERVER_MOTION_SIM_EXECUTED, SERVER_WINGMAN_STATETREE_EXECUTED, SERVER_WINGMAN_PATHFINDING_EXECUTED, SERVER_WINGMAN_STEERING_EXECUTED, SERVER_WINGMAN_INTEGRATION_EXECUTED, SERVER_GROUND_MACHINE_STATETREE_EXECUTED, SERVER_GROUND_MACHINE_PATHFINDING_EXECUTED, SERVER_GROUND_MACHINE_STEERING_EXECUTED, SERVER_GROUND_MACHINE_INTEGRATION_EXECUTED, SERVER_GENERATED_RUNTIME_TRANSFORM, ACTIVE_WITHOUT_FULL_BOOTSTRAP, PARTIAL_ATOMIC_BATCH_COMMIT, BATCH_BASELINE_MISMATCH, BATCH_ROSTER_MISMATCH, TAKEOVER_DEADLINE_BYPASSED, NEW_LEASE_REVOKED_FROM_OLD_FRESHNESS, PENDING_OFFER_PAUSED_ACTIVE_FRESHNESS, OLD_LEASE_ADVANCED_FIRE_SEQUENCE, ACCEPTED_SNAPSHOT_REGRESSION, SNAPSHOT_ADVANCED_WITHOUT_ACCEPTED_CANDIDATE, REJECTED_STATE_RELAYED, UNVALIDATED_STATE_RELAYED, DAMAGE_FROM_UNACCEPTED_POSE, DUPLICATE_SIM_WRITER, WATCHDOG_SCAN_RATE_VIOLATION, STALE_COMBAT_ACCEPTED, UNAVAILABLE_TARGET_ACQUIRED, RESUME_BASE_MISMATCH, POST_DEATH_ACTIVITY, MIN_SPEED_VIOLATION, TURN_RATE_VIOLATION, TRANSFORM_JUMP, CLEARANCE_VIOLATION, FIRE_OUT_OF_RANGE, FIRE_WITHOUT_LOS, FRIENDLY_DAMAGE, DUPLICATE_DAMAGE_COMMIT, VISUAL_EXTRAPOLATION_TIMEOUT, COMMANDER_FIXED_STEP_DROP, NAV_RUNTIME_REBUILD`

正式Gate要求全部计数为0。未知Key、别名或CamelCase直接Fail。

### 9.5 开发命令

- `gs.Wingman.Stats`
- `gs.Wingman.Unit <WingmanId>`
- `gs.Wingman.Group <SimulationGroupId>`
- `gs.Wingman.Nav <FlightId>`
- `gs.Wingman.DebugDraw 0|1|2|3`
- `gs.Wingman.RelayStats`
- `gs.Wingman.LeaseWatchdog`
- `gs.Wingman.QA Start <CoreWingmanSmoke|CoreWingman|GroundMachineIntegration> <Baseline|LagLoss|LeaseLoss|LateJoin>`
- `gs.Wingman.QA Stop`

所有改变状态、生成单位、注入网络或数值覆盖的命令仅编译进Non-Shipping。

### 9.6 证据目录

`Saved/WingmanQA/<campaign_id>/suite-acceptance.json`是Campaign根级唯一汇总，必须列出Catalog Version/Hash、预期22角色、每角色唯一run_id、状态、Gate集合、证据路径和最终结论；缺失、重复或额外角色直接Fail。

`Saved/WingmanQA/<campaign_id>/<run_id>/`每个角色至少包含：

- `manifest.json`
- `acceptance.json`
- `server-events.jsonl`
- 每客户端`client-events.jsonl`
- `client-simulation.csv`
- `server-accepted-snapshots.csv`
- `server-validator-relay.csv`
- `atomic-batches.csv`
- `lease-watchdog.csv`
- `lease-transactions.csv`
- `combat-ledger.jsonl`
- `network-metrics.csv`
- `performance.csv`
- `assertions.json`
- Unreal Insights Trace
- 关键阶段截图/录像索引

以上是正式Campaign要求的完整证据集，目前尚未完整产出。`TestResults/WingmanPlan/`中的实现级自动化、短时Smoke和拆分Harness只能证明各自声明的范围，不得据此填写正式Campaign Pass。

## 10. 双阵营地空部队测试

### 10.1 Fixture

`Wingman.QA.Invasion`在`LVL_CommanderMassPrototype`动态创建：

- Dedicated Server。
- BlueAir、RedAir两个独立客户端。
- S7事务角色按需再启动同队BlueBackupA/BlueBackupB；它们在获Offer前无Lease、无Candidate、无Fire权限。
- 蓝红各1艘玩家Ship和25架Wingman。
- 蓝红各250个Commander Soldier，继续服务器30Hz。
- GroundMachineIntegration Profile适用时，蓝红各1名Ground Owner和至少2台正式Ground机器，确保死亡后仍有存活运动样本。
- 固定seed、路线、障碍、武器和出生槽位。
- Fixture只走正式输入、生成、Lease、Candidate、Fire和Damage入口；禁止SetTransform、直接Kill或直接写Health。

主流程正是“两边生成地面和空中部队，我方飞船移动到敌方阵营”，并从服务器Accepted Snapshot、客户端30Hz Trace和Damage Ledger共同判断移动、转向、攻击和死亡。

### 10.2 Acceptance Profile

- `CoreWingman`：正式Dedicated Server，Ship/Wingman/Commander全部Required。
- `CoreWingmanSmoke`：Editor-Cmd或Listen诊断，永远不能替代Core。
- `GroundMachineIntegration`：正式Ground类、导航、武器、Validator和六向交互全部Required；缺任一前置即Blocked。

正式Catalog升级为`WingmanAcceptanceCatalogV2`。任何旧的服务端运动补位、固定Canonical Slot或Bridge类Gate ID出现在正式Campaign时直接Fail。

### 10.3 22个正式运行角色

| suite_run_role | 目的 |
|---|---|
| Main-S0-S5 | Bootstrap、悬停、侵入、交战、死亡、撤离和补充 |
| S1-Control | Commander隔离对照 |
| S3C-AB | Wingman互射AB消息顺序 |
| S3C-BA | Wingman互射BA消息顺序 |
| S4-Idempotency-FireProposalReplay | FireIntent重复 |
| S4-Idempotency-ProjectileOverlapReplay | ProjectileOverlap重复 |
| S4-Idempotency-Concurrent | 并发重复 |
| S6-CorrectionReverse | Rebase乱序 |
| S6-DeathBeforePose | Death覆盖晚到Accepted Pose |
| S6-LeaseBeforeOldProposal | TransferCommit覆盖旧Owner Proposal |
| S6-RespawnBeforeOldPose | 新Group覆盖旧Group Pose |
| S7-LeaseLoss | Active→Stale→Unavailable→Revoke→Backup恢复 |
| S7-Graceful-FireBeforeCommit | 优雅换主与在途Fire |
| S7-Graceful-DamageAckRace | Damage提交后ACK换主竞态 |
| S7-ActualDisconnect | 当前Active Owner真实断线 |
| S8A-OpeningSpectator | 开局观战Bootstrap |
| S8B-MidCombatJoin | 交战中LateJoin |
| S8C-TransferPendingJoin | Pending Transfer期间LateJoin |
| S9-Control | 500 Commander、无Wingman对照 |
| S9-Feature | 4舰/100 Wingman/500 Commander |
| H4000-OwnerSimulation | 4000无渲染客户端模拟 |
| H4000-ServerValidator | 录制Candidate Validate/Store/Relay压力 |

每个角色恰好一个run_id。Catalog保存版本、SHA-256和Expected Gate集合；缺角色、重复角色、未知Gate或空样本Required Gate都使Campaign失败。

### 10.4 S0～S9阶段

#### S0 Bootstrap

- 地图加载后30秒内完成Nav/Tuning、Roster、Health、Lease和初始Baseline ACK。
- Owner在1秒内启动30Hz私有模拟并提交首个覆盖全部Required Flight的Bootstrap Candidate Batch。
- 全批接纳前Availability=Initializing、Target/Fire/普通单Flight Store/Relay均为0；全批原子接纳后记录`FIRST_BOOTSTRAP_BATCH_ACCEPTED`并切到Active。
- 3秒InitialCandidateDeadline前未完成时在下一1Hz Maintenance Revoke，不能部分Active。
- Commander Lease/Candidate数为0。
- 服务端Wingman运动、StateTree和路径调用数从开局即为0。

#### S1 悬停盘旋与非法Candidate

- 两艘Ship悬停60秒。
- Client Trace必须30Hz；Cruise Capture间隔严格为6个ClientSimTick，模式边界除外。
- 从Client Trace和Accepted Snapshot分别统计最低速度、机头夹角、Bank、间距和环形误差。
- 依次注入非Owner、旧Epoch、错误Lease、重复/倒退FrameSequence、旧/未来CaptureTime、无Grant的5Hz以上Burst、超10Hz Burst、超速、转向跃迁、错误NavChecksum、穿障Candidate、缺片超时Atomic Batch、错误Baseline Hash、旧RosterRevision和不完整IncludedFlightMask。
- 每条非法包恰好Reject一次，AcceptedSequence/Hash、StoreCount、RelayCount和LastValidCandidateTime均不变。
- S1-Control不注入非法包，比较Commander 1800个30Hz FixedStep，DroppedStep=0。

#### S2 侵入、转向与绕障

- Blue Ship通过正式输入沿航点进入Red区域。
- Red Ship通过正式输入执行一段防御转向/拦截路线后悬停；Blue/Red Wingman必须分别产生非零位移与有效Heading变化样本。
- Blue/Red Commander各至少20个单位走正式地面导航并完成一次转向；GroundMachineIntegration中双方Ground Owner各走至少2个航点并完成一次转向。
- 路径至少经过3个Portal，且相对直线偏移至少2×AgentRadius。
- 固定动态障碍横穿目标Flight Corridor，客户端收到ObstacleRevision后进入EmergencyAvoid或重规划。
- 服务器只在Candidate到达时验证Portal/Sweep，不产生路径。
- 首次满足敌对Range/LOS并产生已接纳战斗证据后，服务器授予`HighRate10Hz`，客户端切换Cruise→Combat，Capture间隔从6 Tick变为3 Tick；伪造自报模式不得获得授权。

#### S3 自动交战

Core必须完成：

- Blue/Red Wingman→Enemy Commander Soldier。
- Ship→Commander Soldier。
- Wingman↔Wingman。
- Wingman→Enemy Ship。
- Commander↔Commander。
- Blue与Red Commander Soldier各至少1个通过真实Damage Ledger死亡。

每个方向先生成`TARGET_ELIGIBLE`证据，随后只允许生产客户端本地Acquire和正式FireIntent。服务器不得指定目标或直接注入Damage。

至少一条合法FireIntent引用当前Lease/AuthorityEpoch下、历史窗口内且落后最新值至少2个AcceptedSnapshotSequence的Source Ref并接纳；旧Lease、Stale和过期Ref各拒绝一次。

命名用例`CombatStateTooOld`必须在Availability仍为Active、下一次1Hz Watchdog尚未执行的窗口内，分别制造Source State Age和Target State Age超过CombatStateAge；预期`SourceStateTooOld`和`TargetStateTooOld`各拒绝一次，证明战斗入口即时计算年龄。

GroundMachineIntegration另完成Ground与Commander/Wingman/Ship六个双向方向。

`S3C-AB`和`S3C-BA`使用同一对满血Wingman、同一把致死武器和同一捕获Barrier：AB先释放Blue(A) FireIntent，预期Red死亡、随后Red(B)以`SourceDead`拒绝；BA反向释放并镜像结果。两角色各只允许一次Damage/Death Commit，最终Ledger除胜负Team镜像外一致。

#### S4 死亡与幂等

- Red侧通过真实Fire/Damage链击毁Blue同一Group中3架Wingman；Blue侧另击毁至少1架Red Wingman。
- 目标选择、Range、LOS、冷却、Shot、Damage、HealthZero和Death全部走生产链。
- Blue人口25→22、Red人口25→24，随后S5分别补回。
- GroundMachineIntegration中Blue/Red Ground各至少1台通过敌方正式Fire/Damage链死亡。
- 三个独立幂等run分别重复Fire、Overlap和并发输入；每个目标Damage/Death/Reward/Replenish Schedule CommitCount均为1。
- Dead后迟到Candidate Store/Relay/Fire/Damage应用数为0。

#### S5 撤离、归队与补充

- Blue Ship正式输入撤离。
- 幸存Wingman从Client Trace和Accepted Snapshot证明保持最低速度、正确Bank并归队。
- 每个Carrier独立按Death时间排序、每15秒补1架：Blue 22→23→24→25，Red 24→25。
- 新成员先可靠进入Roster，之后随合法Flight Candidate首次进入Accepted Snapshot；服务器不生成过渡轨迹。

#### S6 弱网、重复与乱序

- 只对Blue Owner施加RTT约500ms、上下行Loss=5%。
- Capture节奏仍是Cruise 6 Tick、Combat 3 Tick；网络只改变到达，不改变客户端模拟。
- 单调新包可接纳，乱序旧包、重复包、旧Lease和旧Group包不Store、不Relay。
- 持续合法5Hz/10Hz输入不得误入Stale；若Loss形成真实超过阈值的空窗，则按1Hz状态规则进入并留下证据。
- 四个脚本必须真实捕获2条消息、释放2条、应用1条、丢弃1条；零捕获不能Pass。
- `S6-CorrectionReverse`：先释放较新的Acceptance/Rebase，再释放较旧Baseline；应用新值，旧值以`BaselineSequenceRegression`丢弃。
- `S6-DeathBeforePose`：先释放可靠Death，再释放更早的Accepted Pose；应用Death，Pose以`EntityDead`丢弃。
- `S6-LeaseBeforeOldProposal`：先释放新Epoch TransferCommit，再释放旧Owner Candidate；应用Commit，Candidate以`LeaseOrAuthorityEpochMismatch`拒绝。
- `S6-RespawnBeforeOldPose`：先释放新GroupGeneration Roster，再释放旧Generation Pose；应用Roster，Pose以`GroupGenerationMismatch`丢弃。

#### S7 Lease Loss、换主与断线

`S7-LeaseLoss`：

1. 丢弃当前Owner的Candidate和Heartbeat。
2. 1Hz Watchdog依次产生Stale、Unavailable、Revoke；DetectionLag在`0～1000ms+1 ServerFrame`。
3. Revoke后服务器选择Backup，并严格执行`Offer → Ready → TransferCommit(new AuthorityEpoch) → Commit Baseline ACK`；Commit冻结的仍是最后Accepted静态快照和Fire高水位。
4. 新Lease在Awaiting状态只走3秒ACK、ACK后3秒Candidate和10秒Overall Deadline；旧Snapshot Age不得使它在下一Maintenance立即Revoke。
5. Backup从Commit Baseline运行私有模拟并提交全组Takeover Candidate Batch；通过后才恢复Active。
6. 从最后Accepted到Backup首个Takeover接纳之间，Snapshot Sequence/Hash/Transform、StoreCount和RelayCount不增加。
7. Stale/Unavailable新Target/Fire/Shot接受数为0；Revoke后旧Owner Candidate/Fire永久接纳数为0。

`S7-Graceful-FireBeforeCommit`：

- 在一次Watchdog Tick刚结束后发送Offer，并在Offer与TransferCommit之间额外接纳一个旧Owner Candidate；该Candidate接纳后、旧Lease仍为Active时，捕获一条已提交但由网络代理暂扣的旧Owner FireIntent。Commit Baseline必须等于更新后的最后Accepted Snapshot，且不得等于更早Preview Baseline。
- 此后暂停旧Owner Candidate，并让Pending Owner延迟Ready；在`Now >= LastValidCandidateTime + 1s`的首个1Hz Watchdog Tick上，旧ActiveLease必须进入Stale并记录`ACTIVE_LEASE_STALE_DURING_PENDING_OFFER`，PendingLease仍保持`AwaitingOfferReady`。测试时钟必须保证Ready仍早于OfferReadyDeadline。
- 旧Lease进入Stale后，Snapshot Sequence/Hash/Transform、StoreCount和RelayCount均不得推进，新Target/Fire接纳数为0；不得因Pending Offer存在而延后Stale/Unavailable/Revoke。随后允许Ready/TransferCommit继续，并只冻结该最后Accepted静态Baseline。
- 顺序为FireSubmitted < TransferCommit < FireReceived < ShotRejected(StaleLease)。
- 该旧Lease Fire不得创建Reservation或推进LastConsumed高水位；Takeover接纳后，新Owner使用Commit冻结高水位+1的首个FireIntent必须能够正常Reserve并接纳。

`S7-Graceful-DamageAckRace`：

- Shot已经服务器接纳，在TransferCommit后、Takeover Batch接纳前让该Shot造成一名成员Death。
- Death/Ledger只提交一次并递增RosterRevision；已暂存Takeover Batch以`RosterRevisionChanged`整批取消，Store/Relay均为0。
- 服务器以存活Mask重冻结`SIM_BASELINE_REVISED`，新Owner ACK后重交全组Batch；旧死亡成员不复活，幸存者Pose Hash不被服务器改写。
- Revised Baseline发送时新的ACK Deadline为`Now+3s`且受原Overall Deadline约束；ACK后Candidate Deadline再次从ACK时起算3秒且同样受Overall约束，旧Deadline不得触发误撤销。
- 本角色仅在Non-Shipping Manifest把ReplenishSeconds覆写为1秒并延迟Revised Baseline ACK约1.5秒，强制产生`deferred_replenish_due_count>=1`；Unavailable期间Spawn=0，Takeover恢复Active后该Schedule恰好Spawn一次。S5仍以正式15秒值验收平衡节奏。
- 延迟Damage ACK/同DedupeKey重放只能返回既有终态，不能再次扣血。

`S7-ActualDisconnect`：

- 真实断开当前Active Owner。
- 正式Profile保留Group；BlueBackupA故意错过OfferReadyDeadline，下一次1Hz维护取消Pending A并选择BlueBackupB。
- BlueBackupB执行Offer/Ready/TransferCommit/Commit Baseline ACK/Takeover全链；无Backup阶段保持NoOwner/Unavailable。
- 新Epoch首个全组Takeover接纳前保持Unavailable，旧Owner消息永久拒绝，服务器运动写入为0。

#### S8 Spectator/LateJoin

- Spectator/LateJoin没有Lease，Candidate/Fire接受数为0。
- S8A在首次交战前连接。
- S8B夹在一次非致死Damage和最终致死Shot之间。
- S8C夹在Lease Offer与TransferCommit之间。
- Bootstrap六Scope在同一Cut逐项Hash一致，2秒内收敛，无幽灵、双Owner或死亡复活。

#### S9 正式规模与压力

- 4个独立Air Owner、4舰、100 Wingman、500 Commander运行10分钟。
- S9-Control同硬件/地图/seed保留4舰和500 Commander但关闭Wingman。
- S9-Feature开启100 Wingman。
- 客户端统计30Hz StateTree/Path/Steering/Integration。
- 服务器统计Packet Gate、Validator、Accepted Store、Relay、Combat Ledger和1Hz Watchdog。
- 服务器运动/StateTree/A*计数必须为0。
- H4000-OwnerSimulation运行4000无渲染客户端实体。
- H4000-ServerValidator播放5Hz Cruise/10Hz Combat录制包，只测试Validate/Store/Relay，不报告成服务器模拟4000架。

## 11. 硬Gate

### 11.1 统计

- P95使用nearest-rank：排序后`x[ceil(0.95*N)-1]`。
- NaN/Inf、Required空样本、样本数低于Catalog最低值均Fail。
- 不跨run、connection、network profile混合样本，除非Catalog明确允许。

### 11.2 零服务端运动

- `server_wingman_motion_step_count=0`
- `server_ground_machine_motion_step_count=0`
- `server_generated_runtime_transform_count=0`
- `server_wingman_state_tree_tick_count=0`
- `server_wingman_path_request_count=0`
- `server_wingman_steering_step_count=0`
- `server_wingman_flight_integration_step_count=0`
- `server_ground_machine_state_tree_tick_count=0`
- `server_ground_machine_path_request_count=0`
- `server_ground_machine_steering_step_count=0`
- `server_ground_machine_movement_integration_step_count=0`
- Accepted Snapshot运动更新数量必须等于合法Candidate中接纳的Flight Payload数量；一次性Spawn Baseline单独计数，不推进AcceptedSnapshotSequence、不作为普通Pose Relay。
- 任一Rejected Candidate对应StoreCount=0、RelayCount=0、AcceptedSequenceDelta=0。
- Store、Acceptance、Observer Apply三方ValidatedPayloadHash必须一致。
- Bootstrap/Resume/Takeover Batch只有全部冻结Mask一次性通过时才能提交；缺片、重复片、超时、Roster/Baseline/Mask不匹配时所有Flight的StoreCount/RelayCount/SequenceDelta均为0。
- Atomic Batch分片暂存期间不得刷新LastValidCandidateTime；`PARTIAL_ATOMIC_BATCH_COMMIT=0`。
- Bootstrap/Resume/Takeover全批成功时，全部FrozenRequired Flight的LastValidCandidateTime必须等于同一AcceptedServerTime；Resume后的首个1Hz Maintenance必须保持Active。Rejected/暂存分片刷新数为0。

### 11.3 频率与Lease

- Client Simulation固定30Hz；Cruise Capture间隔6 Tick，服务器授予HighRate10Hz后的Combat/Hazard为3 Tick，模式切换边界单独标记。
- AllowedUploadRateClass只能由服务器写；无HighRate Grant时超过Cruise5Hz、任何时候超过10Hz的包都必须在昂贵Nav/Sweep前限流。
- 每次HighRate切换必须有当前Lease的可靠GrantRevision、EffectiveClientSimTick和Expiry证据；客户端自报、旧Grant、过期Grant带来的10Hz接纳数为0。
- 服务器Validator Trigger必须是PacketReceived、AtomicBatchAssembled或PendingCarrierResolved，其他触发数为0；AtomicBatchAssembled只能由收包凑齐触发。
- Watchdog间隔`1000ms ± 1 ServerFrame`，每帧逐实体扫描次数为0。
- Initializing超时以及Stale/Unavailable/Revoke事件分别在理论Deadline后的`0～1000ms+1 ServerFrame`发生。
- Stale/Unavailable期间Snapshot Sequence/Hash/Transform不变，新Target/Fire/Shot接受数为0。
- Resume Baseline Sequence/Hash必须等于Transfer/最后Accepted冻结值；首个全组Resume接纳前Availability不得Active。
- Offer→Commit间若有新Accepted Snapshot，Transfer Baseline必须等于Commit瞬间的新值而不等于Preview；精确Revision/Hash ACK后才允许Takeover。
- Revoke/TransferCommit后旧Owner Candidate和FireIntent接纳数永久为0。
- Pending/New Lease Awaiting阶段不使用旧LastValidCandidateAge：Offer后3秒Ready、Commit后3秒ACK、ACK后3秒Candidate和从Offer起10秒Overall Deadline均有事件证据；超时撤销并选择下一Backup，无Backup时NoOwner/Unavailable且运动写入为0。
- Pending Offer不得为旧ActiveLease续鲜或暂停Watchdog：`AwaitingOfferReady`期间旧Owner停包后，首个满足Age>=1秒的1Hz检查必须产生Stale和`ACTIVE_LEASE_STALE_DURING_PENDING_OFFER`；`PENDING_OFFER_PAUSED_ACTIVE_FRESHNESS=0`，其间战斗接纳和快照推进均为0，Pending Owner仍只能按自身Ready/Overall Deadline继续。
- 迟到Ready/ACK/Takeover Batch在消息入口以DeadlineExpired拒绝，接纳数为0；1Hz Maintenance只负责没有消息时的撤销/重选。
- Takeover等待期Death必须取消旧Roster Batch、重冻结Baseline并重ACK；幸存者Accepted Pose Hash不变。实际Replenish Spawn在Active前为0。
- Takeover中的每次SIM_BASELINE_REVISED都必须生成新的RevisedBaselineAckDeadline；精确ACK后才生成新的CandidateDeadline，两者均受原OverallDeadline封顶，旧Deadline导致的撤销数为0。
- Bootstrap Revision变化不得重置InitialCandidateDeadline；Resume Revision变化不得创建Takeover Deadline或延后原Revoke，原Lease Revoke后的ACK/Batch接纳数为0。
- S7-Graceful-DamageAckRace必须满足`deferred_replenish_due_count>=1`、`pre_active_spawn_count=0`、`post_active_spawn_count=1`，并按ScheduleId证明只释放一次；空样本不得Pass。
- Initializing期间只有首个全Required Flight Bootstrap Batch可原子激活；部分Flight接纳、Target/Fire或普通Pose Relay数为0。
- Stale/Unavailable期间当前Lease Owner必须能够进入私有BootstrapOrResume模拟并生成恢复Batch；该私有运动不得写入共享状态，服务器运动计数仍为0。

### 11.4 飞行

- 出生1秒后的Active Owner30Hz样本和Accepted Snapshot样本Speed<2950cm/s失败。
- HeadingRate>20.5°/s或abs(Bank)>45.5°失败。
- 速度方向与机头夹角P95≤5°、Max≤10°。
- 无Spawn/Rebase/Recovery标志时，Owner30Hz单步位移>300cm失败。
- 任意两Wingman逻辑球或Wingman与静态障碍净空<3000cm失败。
- S1最后10秒内/外环径向误差P95≤6000/9000cm，Max≤12000/18000cm；垂直误差P95≤5000cm。
- Stale/Unavailable冻结段不作为“零速服务端模拟样本”进入固定翼统计，而是单独验证不可交互和隐藏。

### 11.5 战斗

- Accepted Shot必须满足Source Ref存在、所有ClientValidatedRelay Source/Target状态均在CombatStateAge内、Active Lease、Availability=Active、Alive、Enemy、Range、LOS、Cooldown和Dedupe。
- 未命中既有DedupeKey的旧Lease/旧AuthorityEpoch Fire必须在Sequence Reserve前拒绝，`reservation_created=0`且高水位Delta=0；新Owner从Transfer Commit高水位+1首发可接纳。
- `CombatStateTooOld`在Availability仍Active且Watchdog未运行时，Source/Target超龄分别以精确Reason拒绝；接纳数均为0。
- Friendly Damage、OutOfRange、NoLOS、Duplicate Damage Commit均为0。
- 每个Damage/Death/Reward CommitCount=1。
- S3C-AB/BA的事件顺序、`SourceDead`拒绝和镜像Ledger必须分别满足Catalog，不能只用汇总伤害数通过。
- Dead后Accepted Store、Relay、Target、Fire和Damage应用数为0。
- 至少3个Blue Wingman死亡；补充严格每15秒1架且ID不复用。
- Main run还要求至少1个Red Wingman、1个Blue Commander和1个Red Commander死亡；不得跨Team/UnitKind互相填充样本。
- Blue/Red Wingman及Blue/Red Commander必须分别存在非零位移样本和至少一次有效Heading变化；按`team + unit_kind`独立统计。
- GroundMachineIntegration要求Blue/Red Ground分别具有移动、转向、合法攻击和至少1个服务器Ledger死亡证据。

### 11.6 网络与表现

- Baseline Candidate→Acceptance P95≤250ms；约500ms RTT弱网≤1000ms。
- Cruise/Combat实收频率、丢包、乱序、RejectReason分别报告。
- 远端视觉外推年龄不得超过500ms；结束后立即淡出并在150ms内隐藏，不得仅冻结可见模型，且不能写回逻辑。
- 可见3D僚机Accepted Pose实收频率不得低于2.5Hz；1Hz样本必须标记为Hidden/Icon LOD。
- Bootstrap六Scope在Ready后2秒内与服务器Hash一致。
- Candidate平均上行、Accepted Pose下行和总带宽满足8.6预算。

### 11.7 性能

- 25 Wingman Owner客户端30Hz Simulation P95目标≤4ms，未实测前为未通过。
- 100 Wingman场景Server Validator+Store+Relay P95目标≤2.5ms/Server Frame、Max≤8ms。
- 1Hz Lease Watchdog P95目标≤0.5ms。
- S9 Feature相对Control的Commander FixedStep/Combat P95回归<5%，DroppedStep=0。
- 每个进程Private Bytes增长<5%，队列结束时Depth=0、DropCount=0。
- H4000两个Harness均报告队列深度、最老年龄、吞吐和结束清空；不能宣称完整4000战局通过。

## 12. 自动化与构建测试

### 12.1 单元/合同

- Stable ID/Generation/Lease/Sequence Gate。
- Candidate量化与ValidatedPayloadHash Golden Bytes。
- Flight RequiredMemberMask原子接纳。
- Bootstrap/Resume/Takeover Batch Envelope、分片重组、Baseline/Roster绑定、超时和全有或全无提交。
- 5Hz/10Hz Capture调度。
- TrailSample严格递增、容量和逐段运动学。
- Ability/Six-Scope可靠ACK先于首个原子Candidate的ActorChannel顺序。
- 新GroupGeneration FlightMode链隔离、Stale/未知私有历史排除和Retained Resume 2秒wire-age上限。
- Active Roster Cut可靠Accepted baseline ACK/Rebase，不伪造Pose或Sequence。
- 全灭Flight补员的单次、不可滑动Freshness grace及1/2/3秒watchdog边界。
- Reject不推进Snapshot/Store/Relay/LastValidTime。
- 1Hz Watchdog Deadline和DetectionLag。
- Source/Target状态年龄与Stale/Unavailable Fire/Target Gate。
- Resume/Transfer Baseline Hash。
- OfferReady及新Lease Takeover三类Deadline、消息入口即时过期Gate、旧Freshness隔离、Backup重选与NoOwner。
- Pending Offer与旧ActiveLease Freshness并行推进：Stale/Unavailable/Revoke、战斗Gate及静态Commit Baseline。
- Pending Batch期间Death/RosterRevision变化、Baseline Revised ACK和Replenish延后。
- Bootstrap/Resume/Takeover三种Baseline Revised Context的Deadline隔离与迟到消息Gate。
- Revised Baseline ACK/Candidate Deadline重置、旧Deadline取消和Overall封顶。
- Damage Dedupe并发。
- Fire Dedupe只读命中、旧Lease先拒绝后不Reserve，以及新Lease从冻结高水位+1正常Reserve。
- Client Mass StateTree ExecutionFlags。
- Dedicated Server Wingman与Ground的StateTree/Path/A*/Steering/Integration零执行证明。

### 12.2 Editor/Bake

- 八叉树/Portal/连通分量。
- 稳定签名和PayloadChecksum。
- Bake后保存/重载。
- 静态几何修改后Stale。
- Missing/Stale/Schema/Checksum Cook失败。
- PIE/Runtime构建次数为0。

### 12.3 联机回归

- Dedicated 22-role Campaign。
- Listen/Editor-Cmd Smoke。
- 正常网、LagLoss、重复、乱序、断线、换主、LateJoin。
- 最新Normal/Weak/Reorder短时真实双进程Listen严格run均通过；该项只代表短时实现级Smoke，不替代Dedicated Campaign或Network Insights。
- Commander 30Hz/B↔B回归。
- 100单位正式性能和拆分4000 Harness。


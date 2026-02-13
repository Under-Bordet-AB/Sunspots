# Sunspots Code Spotlight Report

- Generated: `2026-02-13T21:02:31`
- Branch: `56-bug-fixes-sdk-and-atomic_file_rw`
- Commit: `4c02cba02b53f950c81a2cc52a53dcedc871b9af`

## Legend

- `Len`: function length in lines, `Dec`: decision count, `Nest`: max nesting depth, `Loops`: max loop depth, `Alloc`: allocation/free call count, `Returns`: return statements, `Params`: parameter count

## High Score (Top 3 Longest Functions)

| Rank | Function | Length | Module | Location |
|---:|---|---:|---|---|
| 1 | `main` | 233 | `core` | `core/main.c:17-249` |
| 2 | `http_parse_request` | 227 | `frontend` | `frontend/http_parser.c:123-349` |
| 3 | `ss_log_write_common` | 161 | `sdk` | `sdk/internal/log/ss_log_internal.c:272-432` |

## Low-Hanging Fruit Spotlight (Top 10 by Risk Score)

| Rank | Len | Dec | Nest | Loops | Alloc | Returns | Params | Module | Function | Tags |
|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|
| 1 | 227 | 36 | 5 | 2 | 6 | 20 | 1 | `frontend` | `http_parse_request` (`frontend/http_parser.c:123-349`) | `LONG,BRANCHY,DEEP_NEST,NESTED_LOOPS,ALLOC_HEAVY,MANY_RETURNS,PUBLIC` |
| 2 | 233 | 30 | 7 | 2 | 0 | 3 | 2 | `core` | `main` (`core/main.c:17-249`) | `LONG,BRANCHY,DEEP_NEST,NESTED_LOOPS,PUBLIC` |
| 3 | 161 | 20 | 2 | 0 | 1 | 9 | 7 | `sdk` | `ss_log_write_common` (`sdk/internal/log/ss_log_internal.c:272-432`) | `LONG,BRANCHY,MANY_RETURNS,MANY_PARAMS` |
| 4 | 148 | 28 | 4 | 0 | 3 | 3 | 3 | `core` | `daemon_load_modules` (`core/main.c:256-403`) | `LONG,BRANCHY,DEEP_NEST,ALLOC_HEAVY,PUBLIC` |
| 5 | 120 | 30 | 3 | 1 | 0 | 18 | 3 | `sdk` | `ss_parse_line` (`sdk/internal/db/ss_db_internal.c:393-512`) | `LONG,BRANCHY,MANY_RETURNS` |
| 6 | 134 | 19 | 4 | 2 | 0 | 1 | 1 | `frontend` | `http_worker_thread` (`frontend/http_worker.c:15-148`) | `LONG,BRANCHY,DEEP_NEST,NESTED_LOOPS,PUBLIC` |
| 7 | 117 | 18 | 6 | 2 | 2 | 8 | 3 | `sdk` | `ss_sdk_internal_db_get_last_weeks` (`sdk/internal/db/ss_db_internal.c:768-884`) | `LONG,BRANCHY,DEEP_NEST,NESTED_LOOPS,MANY_RETURNS,PUBLIC` |
| 8 | 103 | 15 | 2 | 0 | 2 | 9 | 1 | `libs` | `curly_init` (`libs/curly.c:37-139`) | `LONG,MANY_RETURNS,PUBLIC` |
| 9 | 96 | 17 | 1 | 0 | 1 | 2 | 1 | `sdk` | `ss_record_to_line` (`sdk/internal/db/ss_db_internal.c:553-648`) | `LONG` |
| 10 | 84 | 13 | 5 | 2 | 0 | 1 | 2 | `fetch` | `main` (`fetch/fetch_manager.c:38-121`) | `LONG,DEEP_NEST,NESTED_LOOPS,PUBLIC` |

## Function Lengths Per Module

### `compute`
- Function count: **11**
- Average length: **26.82** lines

| Length | Location | Function |
|---:|---|---|
| 70 | `compute/compute.c:55-124` | `calculate_simple` |
| 65 | `compute/compute_manager.c:27-91` | `main` |
| 65 | `compute/compute_manager.c:139-203` | `compute_work` |
| 17 | `compute/compute_manager.c:106-122` | `load_data` |
| 14 | `compute/compute.c:3-16` | `data_init` |
| 14 | `compute/compute.c:18-31` | `result_init` |
| 14 | `compute/compute_manager.c:124-137` | `save_result` |
| 12 | `compute/compute_manager.c:93-104` | `heartbeat` |
| 10 | `compute/compute.c:33-42` | `data_dispose` |
| 10 | `compute/compute.c:44-53` | `result_dispose` |
| 4 | `compute/compute_manager.c:205-208` | `cleanup` |

### `config`
- Function count: **17**
- Average length: **21.24** lines

| Length | Location | Function |
|---:|---|---|
| 46 | `config/config.c:38-83` | `read_file_to_string` |
| 42 | `config/config.c:165-206` | `json_set_at_path` |
| 36 | `config/config.c:89-124` | `json_merge` |
| 34 | `config/config.c:269-302` | `config_load_env` |
| 34 | `config/config.c:304-337` | `config_load_args` |
| 31 | `config/config.c:129-159` | `resolve_path` |
| 25 | `config/config.c:243-267` | `config_load_file` |
| 22 | `config/config.c:423-444` | `config_get_string` |
| 16 | `config/config.c:211-226` | `parse_arg_value` |
| 16 | `config/config.c:363-378` | `config_get_bool_or` |
| 13 | `config/config.c:395-407` | `config_get_int` |
| 13 | `config/config.c:409-421` | `config_get_bool` |
| 10 | `config/config.c:352-361` | `config_get_int_or` |
| 10 | `config/config.c:380-389` | `config_get_string_or` |
| 6 | `config/config.c:236-241` | `config_destroy` |
| 4 | `config/config.c:343-346` | `config_get_subtree` |
| 3 | `config/config.c:232-234` | `config_create` |

### `core`
- Function count: **11**
- Average length: **60.18** lines

| Length | Location | Function |
|---:|---|---|
| 233 | `core/main.c:17-249` | `main` |
| 148 | `core/main.c:256-403` | `daemon_load_modules` |
| 79 | `core/main.c:405-483` | `daemon_module_timer_config` |
| 55 | `core/main.c:485-539` | `daemon_spawn_process` |
| 35 | `core/main.c:568-602` | `daemon_read_conf` |
| 28 | `core/main.c:604-631` | `daemon_resolve_project_root` |
| 25 | `core/main.c:541-565` | `daemon_perform_health_check` |
| 22 | `core/main.c:648-669` | `daemon_sigchld_handler` |
| 20 | `core/main.c:677-696` | `daemon_signal_setup` |
| 12 | `core/main.c:635-646` | `daemon_heartbeat_handler` |
| 5 | `core/main.c:671-675` | `daemon_shutdown_handler` |

### `fetch`
- Function count: **17**
- Average length: **23.53** lines

| Length | Location | Function |
|---:|---|---|
| 84 | `fetch/fetch_manager.c:38-121` | `main` |
| 67 | `fetch/apis/fetch_elprisjustnu.c:25-91` | `main` |
| 58 | `fetch/apis/fetch_openmeteo.c:24-81` | `main` |
| 54 | `fetch/fetch_manager.c:123-176` | `load_apis_from_json` |
| 52 | `fetch/fetch_utils.h:5-56` | `fetch_from_url` |
| 13 | `fetch/fetch_utils.h:58-70` | `read_file_to_string` |
| 12 | `fetch/apis/fetch_elprisjustnu.c:93-104` | `heartbeat` |
| 12 | `fetch/apis/fetch_openmeteo.c:83-94` | `heartbeat` |
| 12 | `fetch/fetch_manager.c:178-189` | `heartbeat` |
| 8 | `fetch/fetch_manager.c:191-198` | `handle_child_heartbeat` |
| 7 | `fetch/apis/fetch_openmeteo.c:100-106` | `save_to_database` |
| 6 | `fetch/apis/fetch_elprisjustnu.c:110-115` | `save_to_database` |
| 3 | `fetch/apis/fetch_elprisjustnu.c:106-108` | `normalize_data` |
| 3 | `fetch/apis/fetch_elprisjustnu.c:117-119` | `cleanup` |
| 3 | `fetch/apis/fetch_openmeteo.c:96-98` | `normalize_data` |
| 3 | `fetch/apis/fetch_openmeteo.c:108-110` | `cleanup` |
| 3 | `fetch/fetch_manager.c:200-202` | `cleanup` |

### `frontend`
- Function count: **26**
- Average length: **35.88** lines

| Length | Location | Function |
|---:|---|---|
| 227 | `frontend/http_parser.c:123-349` | `http_parse_request` |
| 134 | `frontend/http_worker.c:15-148` | `http_worker_thread` |
| 87 | `frontend/frontend_main.c:19-105` | `main` |
| 69 | `frontend/endpoints.c:82-150` | `process_request` |
| 55 | `frontend/http_main.c:16-70` | `http_init` |
| 39 | `frontend/endpoints.c:13-51` | `load_file` |
| 36 | `frontend/http_parser.c:436-471` | `http_response_stringify` |
| 34 | `frontend/http_parser.c:64-97` | `CommonResponseMessages` |
| 28 | `frontend/endpoints.c:53-80` | `sanitize_path` |
| 24 | `frontend/http_parser.c:411-434` | `guess_mime_type` |
| 23 | `frontend/http_parser.c:374-396` | `http_response_init` |
| 20 | `frontend/http_parser.c:43-62` | `RequestMethod_tostring` |
| 18 | `frontend/http_main.c:72-89` | `http_accept` |
| 18 | `frontend/http_main.c:91-108` | `http_dispose` |
| 16 | `frontend/http_parser.c:106-121` | `http_get_header` |
| 13 | `frontend/client_queue.c:21-33` | `dequeue_client` |
| 13 | `frontend/http_parser.c:17-29` | `Enum_Method` |
| 12 | `frontend/http_parser.c:351-362` | `http_request_dispose` |
| 12 | `frontend/http_parser.c:398-409` | `http_response_add_header` |
| 12 | `frontend/http_parser.c:473-484` | `http_response_dispose` |
| 11 | `frontend/http_parser.c:31-41` | `Enum_Protocol` |
| 9 | `frontend/client_queue.c:11-19` | `enqueue_client` |
| 9 | `frontend/http_parser.c:364-372` | `http_header_free` |
| 6 | `frontend/http_parser.c:99-104` | `str_to_lower` |
| 4 | `frontend/frontend_main.c:107-110` | `cleanup` |
| 4 | `frontend/http_parser.c:12-15` | `substr` |

### `libs`
- Function count: **20**
- Average length: **28.30** lines

| Length | Location | Function |
|---:|---|---|
| 103 | `libs/curly.c:37-139` | `curly_init` |
| 71 | `libs/atomic_file_rw.h:145-215` | `af_save` |
| 61 | `libs/atomic_file_rw.h:217-277` | `af_read` |
| 39 | `libs/curly.c:226-264` | `curly_cleanup` |
| 33 | `libs/atomic_file_rw.h:88-120` | `af_ensure_parent_dirs` |
| 33 | `libs/linked_list/linked_list.c:89-121` | `LinkedList_remove` |
| 28 | `libs/linked_list/linked_list.c:60-87` | `LinkedList_insert` |
| 24 | `libs/curly.c:201-224` | `curly_reset` |
| 23 | `libs/linked_list/linked_list.c:15-37` | `LinkedList_get_index` |
| 21 | `libs/atomic_file_rw.h:123-143` | `af_write_all` |
| 20 | `libs/linked_list/linked_list.c:39-58` | `LinkedList_append` |
| 19 | `libs/curly.c:141-159` | `curly_make_request` |
| 18 | `libs/curly.c:182-199` | `curly_read_response` |
| 17 | `libs/curly.c:19-35` | `write_memory_callback` |
| 17 | `libs/linked_list/linked_list.c:130-146` | `LinkedList_clear` |
| 12 | `libs/curly.c:161-172` | `curly_poll` |
| 9 | `libs/linked_list/linked_list.c:5-13` | `LinkedList_create` |
| 7 | `libs/curly.c:174-180` | `curly_is_running` |
| 6 | `libs/linked_list/linked_list.c:123-128` | `LinkedList_pop` |
| 5 | `libs/linked_list/linked_list.c:147-151` | `LinkedList_dispose` |

### `sdk`
- Function count: **52**
- Average length: **27.13** lines

| Length | Location | Function |
|---:|---|---|
| 161 | `sdk/internal/log/ss_log_internal.c:272-432` | `ss_log_write_common` |
| 120 | `sdk/internal/db/ss_db_internal.c:393-512` | `ss_parse_line` |
| 117 | `sdk/internal/db/ss_db_internal.c:768-884` | `ss_sdk_internal_db_get_last_weeks` |
| 96 | `sdk/internal/db/ss_db_internal.c:553-648` | `ss_record_to_line` |
| 65 | `sdk/internal/db/ss_db_internal.c:702-766` | `ss_sdk_internal_db_write_record` |
| 53 | `sdk/ss_sdk.c:30-82` | `ss_sdk_validate_record` |
| 51 | `sdk/internal/log/ss_log_internal.c:113-163` | `ss_escape_text` |
| 48 | `sdk/internal/db/ss_db_internal.c:164-211` | `ss_escape` |
| 47 | `sdk/internal/db/ss_db_internal.c:92-138` | `ss_read_all_fd` |
| 44 | `sdk/internal/log/ss_log_internal.c:165-208` | `ss_extract_json_log_path` |
| 44 | `sdk/internal/log/ss_log_internal.c:227-270` | `ss_log_write_line` |
| 31 | `sdk/internal/db/ss_db_internal.c:521-551` | `ss_record_identity_equal` |
| 29 | `sdk/internal/db/ss_db_internal.c:213-241` | `ss_unescape_inplace` |
| 28 | `sdk/internal/db/ss_db_internal.c:318-345` | `ss_parse_double` |
| 26 | `sdk/internal/db/ss_db_internal.c:65-90` | `ss_ensure_parent_dirs` |
| 26 | `sdk/internal/log/ss_log_internal.c:46-71` | `ss_ensure_parent_dirs` |
| 25 | `sdk/internal/db/ss_db_internal.c:650-674` | `ss_record_cmp` |
| 25 | `sdk/internal/db/ss_db_internal.c:676-700` | `ss_line_is_duplicate` |
| 23 | `sdk/internal/db/ss_db_internal.c:140-162` | `ss_write_all` |
| 23 | `sdk/internal/db/ss_db_internal.c:248-270` | `ss_record_free_strings` |
| 23 | `sdk/internal/log/ss_log_internal.c:89-111` | `ss_write_all` |
| 19 | `sdk/internal/db/ss_db_internal.c:366-384` | `ss_parse_f64_bits` |
| 17 | `sdk/internal/db/ss_db_internal.c:272-288` | `ss_split_fields` |
| 16 | `sdk/internal/db/ss_db_internal.c:28-43` | `ss_strdup_local` |
| 16 | `sdk/internal/log/ss_log_internal.c:29-44` | `ss_strdup_local` |
| 16 | `sdk/internal/log/ss_log_internal.c:210-225` | `ss_get_log_path` |
| 15 | `sdk/internal/db/ss_db_internal.c:886-900` | `ss_sdk_internal_db_free_records` |
| 15 | `sdk/internal/log/ss_log_internal.c:73-87` | `ss_level_to_string` |
| 15 | `sdk/ss_sdk.c:9-23` | `ss_sdk_is_valid_value_type` |
| 13 | `sdk/internal/db/ss_db_internal.c:290-302` | `ss_parse_i64` |
| 13 | `sdk/internal/db/ss_db_internal.c:304-316` | `ss_parse_int` |
| 11 | `sdk/internal/log/ss_log_internal.c:445-455` | `ss_sdk_internal_log_write_fields` |
| 11 | `sdk/ss_sdk.c:117-127` | `ss_sdk_log_write_fields` |
| 10 | `sdk/internal/db/ss_db_internal.c:54-63` | `ss_checked_add` |
| 10 | `sdk/internal/log/ss_log_internal.c:18-27` | `ss_checked_add` |
| 10 | `sdk/internal/log/ss_log_internal.c:434-443` | `ss_sdk_internal_log_write_auto` |
| 10 | `sdk/ss_canonical.c:11-20` | `ss_metric_meta_get` |
| 10 | `sdk/ss_sdk.c:106-115` | `ss_sdk_log_write_auto` |
| 8 | `sdk/internal/db/ss_db_internal.c:45-52` | `ss_db_path` |
| 8 | `sdk/ss_canonical.c:22-29` | `ss_metric_name` |
| 8 | `sdk/ss_sdk.c:84-91` | `ss_sdk_db_write_record` |
| 7 | `sdk/internal/db/ss_db_internal.c:352-358` | `ss_is_valid_value_type` |
| 7 | `sdk/ss_sdk.c:93-99` | `ss_sdk_db_get_last_weeks` |
| 6 | `sdk/internal/db/ss_db_internal.c:386-391` | `ss_f64_to_bits` |
| 6 | `sdk/internal/db/ss_db_internal.c:514-519` | `ss_string_equal_nullable` |
| 5 | `sdk/internal/db/ss_db_internal.c:360-364` | `ss_is_valid_data_kind` |
| 4 | `sdk/internal/db/ss_db_internal.c:243-246` | `ss_record_reset` |
| 4 | `sdk/internal/db/ss_db_internal.c:347-350` | `ss_is_valid_metric` |
| 4 | `sdk/internal/log/ss_log_internal.c:457-460` | `ss_sdk_internal_log_shutdown` |
| 4 | `sdk/ss_sdk.c:25-28` | `ss_sdk_is_nonempty` |
| 4 | `sdk/ss_sdk.c:101-104` | `ss_sdk_db_free_records` |
| 4 | `sdk/ss_sdk.c:129-132` | `ss_sdk_shutdown` |

### `transform`
- Function count: **3**
- Average length: **51.33** lines

| Length | Location | Function |
|---:|---|---|
| 78 | `transform/weather/openmeteo.c:10-87` | `transform_openmeteo_weather` |
| 67 | `transform/weather/openmeteo.c:89-155` | `transform_openmeteo_solar` |
| 9 | `transform/weather/weather_model.c:4-12` | `weather_data_init` |

### `utils`
- Function count: **2**
- Average length: **6.00** lines

| Length | Location | Function |
|---:|---|---|
| 9 | `utils/unit_utils.c:10-18` | `iso8601_to_unix` |
| 3 | `utils/unit_utils.c:6-8` | `fahrenheit_to_celsius` |

## Spotlight Per Module (Top 5 by Risk Score)

### `compute`
- Function count: **11**

| Len | Dec | Nest | Loops | Function | Tags |
|---:|---:|---:|---:|---|---|
| 70 | 18 | 3 | 0 | `calculate_simple` (`compute/compute.c:55-124`) | `BRANCHY,PUBLIC` |
| 65 | 10 | 3 | 1 | `main` (`compute/compute_manager.c:27-91`) | `PUBLIC` |
| 65 | 10 | 2 | 0 | `compute_work` (`compute/compute_manager.c:139-203`) | `MANY_RETURNS,PUBLIC` |
| 17 | 2 | 1 | 0 | `load_data` (`compute/compute_manager.c:106-122`) | `PUBLIC` |
| 14 | 2 | 1 | 0 | `data_init` (`compute/compute.c:3-16`) | `PUBLIC` |

### `config`
- Function count: **17**

| Len | Dec | Nest | Loops | Function | Tags |
|---:|---:|---:|---:|---|---|
| 42 | 10 | 3 | 1 | `json_set_at_path` (`config/config.c:165-206`) | `` |
| 46 | 8 | 1 | 0 | `read_file_to_string` (`config/config.c:38-83`) | `MANY_RETURNS` |
| 34 | 9 | 4 | 1 | `config_load_args` (`config/config.c:304-337`) | `DEEP_NEST,PUBLIC` |
| 36 | 9 | 3 | 1 | `json_merge` (`config/config.c:89-124`) | `` |
| 31 | 7 | 2 | 1 | `resolve_path` (`config/config.c:129-159`) | `` |

### `core`
- Function count: **11**

| Len | Dec | Nest | Loops | Function | Tags |
|---:|---:|---:|---:|---|---|
| 233 | 30 | 7 | 2 | `main` (`core/main.c:17-249`) | `LONG,BRANCHY,DEEP_NEST,NESTED_LOOPS,PUBLIC` |
| 148 | 28 | 4 | 0 | `daemon_load_modules` (`core/main.c:256-403`) | `LONG,BRANCHY,DEEP_NEST,ALLOC_HEAVY,PUBLIC` |
| 79 | 9 | 2 | 0 | `daemon_module_timer_config` (`core/main.c:405-483`) | `PUBLIC` |
| 55 | 6 | 2 | 0 | `daemon_spawn_process` (`core/main.c:485-539`) | `PUBLIC` |
| 35 | 5 | 2 | 0 | `daemon_read_conf` (`core/main.c:568-602`) | `PUBLIC` |

### `fetch`
- Function count: **17**

| Len | Dec | Nest | Loops | Function | Tags |
|---:|---:|---:|---:|---|---|
| 84 | 13 | 5 | 2 | `main` (`fetch/fetch_manager.c:38-121`) | `LONG,DEEP_NEST,NESTED_LOOPS,PUBLIC` |
| 67 | 9 | 2 | 1 | `main` (`fetch/apis/fetch_elprisjustnu.c:25-91`) | `PUBLIC` |
| 58 | 9 | 2 | 1 | `main` (`fetch/apis/fetch_openmeteo.c:24-81`) | `PUBLIC` |
| 54 | 8 | 2 | 1 | `load_apis_from_json` (`fetch/fetch_manager.c:123-176`) | `PUBLIC` |
| 52 | 8 | 2 | 1 | `fetch_from_url` (`fetch/fetch_utils.h:5-56`) | `MANY_RETURNS,PUBLIC` |

### `frontend`
- Function count: **26**

| Len | Dec | Nest | Loops | Function | Tags |
|---:|---:|---:|---:|---|---|
| 227 | 36 | 5 | 2 | `http_parse_request` (`frontend/http_parser.c:123-349`) | `LONG,BRANCHY,DEEP_NEST,NESTED_LOOPS,ALLOC_HEAVY,MANY_RETURNS,PUBLIC` |
| 134 | 19 | 4 | 2 | `http_worker_thread` (`frontend/http_worker.c:15-148`) | `LONG,BRANCHY,DEEP_NEST,NESTED_LOOPS,PUBLIC` |
| 87 | 12 | 4 | 1 | `main` (`frontend/frontend_main.c:19-105`) | `LONG,DEEP_NEST,PUBLIC` |
| 69 | 13 | 2 | 0 | `process_request` (`frontend/endpoints.c:82-150`) | `MANY_RETURNS,PUBLIC` |
| 55 | 5 | 1 | 1 | `http_init` (`frontend/http_main.c:16-70`) | `MANY_RETURNS,PUBLIC` |

### `libs`
- Function count: **20**

| Len | Dec | Nest | Loops | Function | Tags |
|---:|---:|---:|---:|---|---|
| 103 | 15 | 2 | 0 | `curly_init` (`libs/curly.c:37-139`) | `LONG,MANY_RETURNS,PUBLIC` |
| 71 | 9 | 1 | 0 | `af_save` (`libs/atomic_file_rw.h:145-215`) | `MANY_RETURNS,PUBLIC` |
| 61 | 11 | 2 | 1 | `af_read` (`libs/atomic_file_rw.h:217-277`) | `MANY_RETURNS,PUBLIC` |
| 33 | 7 | 3 | 1 | `af_ensure_parent_dirs` (`libs/atomic_file_rw.h:88-120`) | `` |
| 39 | 8 | 1 | 0 | `curly_cleanup` (`libs/curly.c:226-264`) | `PUBLIC` |

### `sdk`
- Function count: **52**

| Len | Dec | Nest | Loops | Function | Tags |
|---:|---:|---:|---:|---|---|
| 161 | 20 | 2 | 0 | `ss_log_write_common` (`sdk/internal/log/ss_log_internal.c:272-432`) | `LONG,BRANCHY,MANY_RETURNS,MANY_PARAMS` |
| 120 | 30 | 3 | 1 | `ss_parse_line` (`sdk/internal/db/ss_db_internal.c:393-512`) | `LONG,BRANCHY,MANY_RETURNS` |
| 117 | 18 | 6 | 2 | `ss_sdk_internal_db_get_last_weeks` (`sdk/internal/db/ss_db_internal.c:768-884`) | `LONG,BRANCHY,DEEP_NEST,NESTED_LOOPS,MANY_RETURNS,PUBLIC` |
| 96 | 17 | 1 | 0 | `ss_record_to_line` (`sdk/internal/db/ss_db_internal.c:553-648`) | `LONG` |
| 53 | 19 | 2 | 0 | `ss_sdk_validate_record` (`sdk/ss_sdk.c:30-82`) | `BRANCHY,MANY_RETURNS` |

### `transform`
- Function count: **3**

| Len | Dec | Nest | Loops | Function | Tags |
|---:|---:|---:|---:|---|---|
| 78 | 10 | 2 | 0 | `transform_openmeteo_weather` (`transform/weather/openmeteo.c:10-87`) | `MANY_RETURNS,PUBLIC` |
| 67 | 12 | 3 | 1 | `transform_openmeteo_solar` (`transform/weather/openmeteo.c:89-155`) | `MANY_RETURNS,PUBLIC` |
| 9 | 0 | 0 | 0 | `weather_data_init` (`transform/weather/weather_model.c:4-12`) | `PUBLIC` |

### `utils`
- Function count: **2**

| Len | Dec | Nest | Loops | Function | Tags |
|---:|---:|---:|---:|---|---|
| 9 | 1 | 1 | 0 | `iso8601_to_unix` (`utils/unit_utils.c:10-18`) | `PUBLIC` |
| 3 | 0 | 0 | 0 | `fahrenheit_to_celsius` (`utils/unit_utils.c:6-8`) | `PUBLIC` |

## Static Discipline Candidates

| Module | Location | Function | Reason |
|---|---|---|---|
| `compute` | `compute/compute_manager.c:106` | `load_data` | used only in defining file and not declared in headers |
| `compute` | `compute/compute_manager.c:124` | `save_result` | used only in defining file and not declared in headers |
| `compute` | `compute/compute_manager.c:139` | `compute_work` | used only in defining file and not declared in headers |
| `core` | `core/main.c:256` | `daemon_load_modules` | used only in defining file and not declared in headers |
| `core` | `core/main.c:648` | `daemon_sigchld_handler` | used only in defining file and not declared in headers |
| `fetch` | `fetch/fetch_manager.c:123` | `load_apis_from_json` | used only in defining file and not declared in headers |
| `fetch` | `fetch/fetch_manager.c:191` | `handle_child_heartbeat` | used only in defining file and not declared in headers |
| `frontend` | `frontend/endpoints.c:13` | `load_file` | used only in defining file and not declared in headers |
| `frontend` | `frontend/endpoints.c:53` | `sanitize_path` | used only in defining file and not declared in headers |
| `frontend` | `frontend/http_parser.c:12` | `substr` | used only in defining file and not declared in headers |
| `frontend` | `frontend/http_parser.c:64` | `CommonResponseMessages` | used only in defining file and not declared in headers |
| `libs` | `libs/curly.c:19` | `write_memory_callback` | used only in defining file and not declared in headers |

## Const Discipline Candidates

| Module | Location | Function | Parameter | Suggested Direction |
|---|---|---|---|---|
| `compute` | `compute/compute.c:55` | `calculate_simple` | `data` (`data_t* data`) | consider `const` on pointee |
| `compute` | `compute/compute_manager.c:27` | `main` | `argv` (`char* argv[]`) | consider `const` on pointee |
| `compute` | `compute/compute_manager.c:124` | `save_result` | `result` (`result_t* result`) | consider `const` on pointee |
| `config` | `config/config.c:243` | `config_load_file` | `cfg` (`config* cfg`) | consider `const` on pointee |
| `config` | `config/config.c:269` | `config_load_env` | `cfg` (`config* cfg`) | consider `const` on pointee |
| `config` | `config/config.c:304` | `config_load_args` | `argv` (`char** argv`) | consider `const` on pointee |
| `config` | `config/config.c:304` | `config_load_args` | `cfg` (`config* cfg`) | consider `const` on pointee |
| `config` | `config/config.c:423` | `config_get_string` | `out` (`char* out`) | consider `const` on pointee |
| `core` | `core/main.c:17` | `main` | `argv` (`char **argv`) | consider `const` on pointee |
| `core` | `core/main.c:635` | `daemon_heartbeat_handler` | `context` (`void *context`) | consider `const` on pointee |
| `core` | `core/main.c:635` | `daemon_heartbeat_handler` | `info` (`siginfo_t *info`) | consider `const` on pointee |
| `fetch` | `fetch/apis/fetch_elprisjustnu.c:25` | `main` | `argv` (`char* argv[]`) | consider `const` on pointee |
| `fetch` | `fetch/apis/fetch_elprisjustnu.c:106` | `normalize_data` | `buffer` (`char** buffer`) | consider `const` on pointee |
| `fetch` | `fetch/apis/fetch_elprisjustnu.c:106` | `normalize_data` | `raw` (`char* raw`) | consider `const` on pointee |
| `fetch` | `fetch/apis/fetch_elprisjustnu.c:110` | `save_to_database` | `buffer` (`char* buffer`) | consider `const` on pointee |
| `fetch` | `fetch/apis/fetch_openmeteo.c:24` | `main` | `argv` (`char* argv[]`) | consider `const` on pointee |
| `fetch` | `fetch/apis/fetch_openmeteo.c:96` | `normalize_data` | `buffer` (`char** buffer`) | consider `const` on pointee |
| `fetch` | `fetch/apis/fetch_openmeteo.c:96` | `normalize_data` | `raw` (`char* raw`) | consider `const` on pointee |
| `fetch` | `fetch/apis/fetch_openmeteo.c:100` | `save_to_database` | `buffer` (`char* buffer`) | consider `const` on pointee |
| `fetch` | `fetch/fetch_manager.c:38` | `main` | `argv` (`char* argv[]`) | consider `const` on pointee |
| `fetch` | `fetch/fetch_manager.c:191` | `handle_child_heartbeat` | `context` (`void* context`) | consider `const` on pointee |
| `fetch` | `fetch/fetch_utils.h:5` | `fetch_from_url` | `url` (`char* url`) | consider `const` on pointee |
| `frontend` | `frontend/frontend_main.c:19` | `main` | `argv` (`char* argv[]`) | consider `const` on pointee |
| `frontend` | `frontend/http_main.c:72` | `http_accept` | `server` (`http_server* server`) | consider `const` on pointee |
| `frontend` | `frontend/http_parser.c:106` | `http_get_header` | `req` (`http_request* req`) | consider `const` on pointee |
| `frontend` | `frontend/http_parser.c:364` | `http_header_free` | `header_var` (`void* header_var`) | consider `const` on pointee |
| `frontend` | `frontend/http_parser.c:398` | `http_response_add_header` | `response` (`http_response* response`) | consider `const` on pointee |
| `frontend` | `frontend/http_parser.c:436` | `http_response_stringify` | `response` (`http_response* response`) | consider `const` on pointee |
| `frontend` | `frontend/http_worker.c:15` | `http_worker_thread` | `arg` (`void* arg`) | consider `const` on pointee |
| `libs` | `libs/curly.c:19` | `write_memory_callback` | `contents` (`void* contents`) | consider `const` on pointee |
| `libs` | `libs/curly.c:19` | `write_memory_callback` | `user_p` (`void* user_p`) | consider `const` on pointee |
| `libs` | `libs/curly.c:141` | `curly_make_request` | `client` (`curly_t** client`) | consider `const` on pointee |
| `libs` | `libs/curly.c:161` | `curly_poll` | `client` (`curly_t** client`) | consider `const` on pointee |
| `libs` | `libs/curly.c:174` | `curly_is_running` | `client` (`curly_t** client`) | consider `const` on pointee |
| `libs` | `libs/curly.c:182` | `curly_read_response` | `client` (`curly_t** client`) | consider `const` on pointee |
| `libs` | `libs/curly.c:201` | `curly_reset` | `client` (`curly_t** client`) | consider `const` on pointee |
| `libs` | `libs/linked_list/linked_list.c:15` | `LinkedList_get_index` | `list` (`LinkedList *list`) | consider `const` on pointee |
| `libs` | `libs/linked_list/linked_list.c:39` | `LinkedList_append` | `item` (`void *item`) | consider `const` on pointee |
| `libs` | `libs/linked_list/linked_list.c:60` | `LinkedList_insert` | `item` (`void *item`) | consider `const` on pointee |
| `libs` | `libs/linked_list/linked_list.c:123` | `LinkedList_pop` | `list` (`LinkedList *list`) | consider `const` on pointee |
| `sdk` | `sdk/internal/db/ss_db_internal.c:54` | `ss_checked_add` | `current_size` (`size_t *current_size`) | consider `const` on pointee |
| `sdk` | `sdk/internal/db/ss_db_internal.c:213` | `ss_unescape_inplace` | `s` (`char *s`) | consider `const` on pointee |
| `sdk` | `sdk/internal/db/ss_db_internal.c:243` | `ss_record_reset` | `rec` (`ss_sdk_record *rec`) | consider `const` on pointee |
| `sdk` | `sdk/internal/db/ss_db_internal.c:272` | `ss_split_fields` | `line` (`char *line`) | consider `const` on pointee |
| `sdk` | `sdk/internal/db/ss_db_internal.c:366` | `ss_parse_f64_bits` | `out` (`double *out`) | consider `const` on pointee |
| `sdk` | `sdk/internal/db/ss_db_internal.c:393` | `ss_parse_line` | `line` (`char *line`) | consider `const` on pointee |
| `sdk` | `sdk/internal/db/ss_db_internal.c:676` | `ss_line_is_duplicate` | `all` (`char *all`) | consider `const` on pointee |
| `sdk` | `sdk/internal/db/ss_db_internal.c:886` | `ss_sdk_internal_db_free_records` | `records` (`ss_sdk_record *records`) | consider `const` on pointee |
| `sdk` | `sdk/internal/log/ss_log_internal.c:18` | `ss_checked_add` | `current_size` (`size_t *current_size`) | consider `const` on pointee |
| `sdk` | `sdk/ss_sdk.c:93` | `ss_sdk_db_get_last_weeks` | `out_count` (`size_t *out_count`) | consider `const` on pointee |
| `sdk` | `sdk/ss_sdk.c:93` | `ss_sdk_db_get_last_weeks` | `out_records` (`ss_sdk_record **out_records`) | consider `const` on pointee |
| `sdk` | `sdk/ss_sdk.c:101` | `ss_sdk_db_free_records` | `records` (`ss_sdk_record *records`) | consider `const` on pointee |

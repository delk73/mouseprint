PRAGMA foreign_keys=ON;

CREATE TABLE collector_runs (
  run_id INTEGER PRIMARY KEY,
  started_wallclock_us INTEGER NOT NULL,
  started_monotonic_us INTEGER NOT NULL,
  ended_wallclock_us INTEGER,
  events_seen INTEGER
);
CREATE TABLE devices (device_id TEXT PRIMARY KEY, device_name TEXT NOT NULL);
CREATE TABLE raw_input_events (
  event_id INTEGER PRIMARY KEY,
  run_id INTEGER NOT NULL REFERENCES collector_runs(run_id),
  receive_sequence INTEGER NOT NULL,
  source_time_us INTEGER,
  device_id TEXT NOT NULL,
  event_type TEXT NOT NULL,
  dx_accelerated_collector REAL,
  dy_accelerated_collector REAL,
  dx_unaccelerated REAL,
  dy_unaccelerated REAL,
  button INTEGER,
  button_state TEXT,
  scroll_source TEXT,
  scroll_x REAL,
  scroll_y REAL
);
CREATE TABLE pointer_context (
  context_id INTEGER PRIMARY KEY,
  run_id INTEGER NOT NULL REFERENCES collector_runs(run_id),
  sample_monotonic_us INTEGER NOT NULL,
  request_start_us INTEGER NOT NULL,
  request_end_us INTEGER NOT NULL,
  request_latency_us INTEGER NOT NULL,
  sample_status TEXT NOT NULL,
  cursor_x REAL,
  cursor_y REAL,
  monitor_id INTEGER,
  monitor_name TEXT,
  workspace_id INTEGER,
  workspace_name TEXT,
  active_window_address TEXT,
  active_app TEXT,
  active_window_class TEXT
);
CREATE TABLE input_context_matches (
  match_id INTEGER PRIMARY KEY,
  run_id INTEGER NOT NULL REFERENCES collector_runs(run_id),
  raw_event_id INTEGER NOT NULL REFERENCES raw_input_events(event_id),
  context_id INTEGER REFERENCES pointer_context(context_id),
  match_status TEXT NOT NULL,
  context_delta_us INTEGER,
  absolute_delta_us INTEGER,
  tolerance_us INTEGER NOT NULL
);
CREATE TABLE movement_episodes (
  episode_id INTEGER PRIMARY KEY,
  run_id INTEGER NOT NULL REFERENCES collector_runs(run_id),
  device_id TEXT NOT NULL REFERENCES devices(device_id),
  start_time_us INTEGER NOT NULL,
  end_time_us INTEGER NOT NULL,
  duration_us INTEGER NOT NULL,
  idle_gap_us INTEGER NOT NULL,
  end_reason TEXT NOT NULL,
  motion_event_count INTEGER NOT NULL,
  terminates_in_button_press INTEGER NOT NULL,
  device_metric_status TEXT NOT NULL,
  device_path_distance REAL,
  device_average_velocity REAL,
  device_peak_velocity REAL,
  device_directional_reversal_count INTEGER,
  compositor_metric_status TEXT NOT NULL,
  compositor_start_x REAL,
  compositor_start_y REAL,
  compositor_end_x REAL,
  compositor_end_y REAL,
  compositor_path_distance REAL,
  compositor_displacement REAL,
  compositor_path_efficiency REAL,
  compositor_average_velocity REAL,
  compositor_peak_velocity REAL
);
CREATE TABLE movement_episode_members (
  episode_id INTEGER NOT NULL REFERENCES movement_episodes(episode_id),
  ordinal INTEGER NOT NULL,
  raw_event_id INTEGER NOT NULL REFERENCES raw_input_events(event_id),
  match_id INTEGER REFERENCES input_context_matches(match_id),
  member_role TEXT NOT NULL,
  PRIMARY KEY (episode_id, ordinal)
);
CREATE TABLE movement_episode_trajectory_points (
  episode_id INTEGER NOT NULL REFERENCES movement_episodes(episode_id),
  ordinal INTEGER NOT NULL,
  raw_event_id INTEGER NOT NULL REFERENCES raw_input_events(event_id),
  match_id INTEGER REFERENCES input_context_matches(match_id),
  source_time_us INTEGER,
  device_dx REAL,
  device_dy REAL,
  device_cumulative_x REAL,
  device_cumulative_y REAL,
  device_cumulative_path REAL,
  context_id INTEGER REFERENCES pointer_context(context_id),
  context_sample_time_us INTEGER,
  compositor_x REAL,
  compositor_y REAL,
  compositor_cumulative_path REAL,
  PRIMARY KEY (episode_id, ordinal)
);

INSERT INTO collector_runs VALUES (1, 1000000, 10, 3000000, 9);
INSERT INTO collector_runs VALUES (2, 4000000, 40, 9000000, 4);
INSERT INTO collector_runs VALUES (3, 10000000, 100, NULL, 2);
INSERT INTO collector_runs VALUES (4, 11000000, 110, 12000000, 1);
INSERT INTO collector_runs VALUES (5, 13000000, 130, 14000000, 0);
INSERT INTO collector_runs VALUES (0, 0, 0, 500, 1);
INSERT INTO devices VALUES ('mouse-a', 'Mouse A');
INSERT INTO devices VALUES ('mouse-b', 'Mouse B');
INSERT INTO devices VALUES ('mouse-c', 'Mouse C');

INSERT INTO raw_input_events(event_id,run_id,receive_sequence,source_time_us,device_id,event_type)
VALUES (101,1,1,100, 'mouse-a','MOTION'), (102,1,2,110,'mouse-a','MOTION'),
       (103,1,3,120,'mouse-a','MOTION'), (104,1,4,130,'mouse-b','MOTION'),
       (105,1,5,140,'mouse-b','MOTION'), (106,1,6,150,'mouse-b','MOTION'),
       (107,1,7,160,'mouse-a','BUTTON_DOWN'), (108,1,8,170,'mouse-a','SCROLL'),
       (201,2,1,200,'mouse-a','MOTION'),
       (401,4,1,400,'mouse-c','MOTION'),
       (601,0,1,50,'mouse-c','MOTION');

INSERT INTO pointer_context(context_id,run_id,sample_monotonic_us,request_start_us,request_end_us,
  request_latency_us,sample_status,cursor_x,cursor_y)
VALUES (501,1,100,100,101,1,'ok',10,20), (502,1,110,110,111,1,'ok',11,20),
       (503,1,130,130,131,1,'ok',100,200), (504,1,120,120,121,1,'cursor_request_failed',NULL,NULL),
       (505,1,140,140,141,1,'cursor_request_failed',NULL,NULL),
       (506,1,150,150,151,1,'ok',110,200), (601,2,200,200,201,1,'ok',30,40);

INSERT INTO input_context_matches(match_id,run_id,raw_event_id,context_id,match_status,
  context_delta_us,absolute_delta_us,tolerance_us)
VALUES (701,1,101,501,'matched',0,0,25000),
       (702,1,102,501,'matched',0,0,25000),
       (703,1,103,504,'unmatched_context_error',0,0,25000),
       (704,1,104,503,'matched',0,0,25000),
       (705,1,105,505,'unmatched_context_error',0,0,25000),
       (706,1,106,506,'matched',0,0,25000),
       (707,1,107,NULL,'unmatched_outside_tolerance',NULL,NULL,25000),
       (708,1,108,NULL,'unmatched_no_context',NULL,NULL,25000),
       (801,2,201,601,'matched',0,0,25000);

INSERT INTO movement_episodes VALUES
 (11,1,'mouse-a',100,120,20,100000,'run_end',3,0,'available',3,1,2,0,'available',10,20,12,20,2,2,1,1,1),
 (12,1,'mouse-b',130,150,20,100000,'idle_gap',3,0,'missing_unaccelerated_values',NULL,NULL,NULL,NULL,
   'context_sampling_failed',NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
 (21,2,'mouse-a',200,200,0,100000,'run_end',1,0,'available',0,0,0,0,'available',30,40,30,40,0,0,NULL,NULL,NULL),
 (61,0,'mouse-c',50,50,0,100000,'run_end',1,0,'missing_unaccelerated_values',NULL,NULL,NULL,NULL,
  'context_sampling_failed',NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL);

INSERT INTO movement_episode_members VALUES
 (11,0,101,701,'motion'), (11,1,102,702,'motion'), (11,2,103,703,'motion'),
 (12,0,104,704,'motion'), (12,1,105,705,'motion'), (12,2,106,706,'motion'),
 (21,0,201,801,'motion'),
 (61,0,601,NULL,'motion');

INSERT INTO movement_episode_trajectory_points VALUES
 (11,0,101,701,100,1,0,1,0,0,501,100,10,20,0),
 (11,1,102,702,110,1,0,2,0,1,501,100,10,20,0),
 (11,2,103,703,120,1,0,3,0,2,504,120,NULL,NULL,NULL),
 (12,0,104,704,130,1,0,1,0,1,503,130,100,200,0),
 (12,1,105,705,140,NULL,NULL,NULL,NULL,NULL,505,140,NULL,NULL,NULL),
 (12,2,106,706,150,2,3,NULL,NULL,NULL,506,150,110,200,NULL),
 (21,0,201,801,200,0,0,0,0,0,601,200,30,40,0);

-- KEYS - profile name
-- ARGV[1] - port speed
-- ARGV[2] - cable length
-- ARGV[3] - port mtu
-- ARGV[4] - gearbox delay

-- Parameters retried from databases:
-- From CONFIG_DB.LOSSLESS_TRAFFIC_PATTERN
--      small packet percentage:    the parameter which is used to control worst case regarding the cell utilization
--      mtu:                        the mtu of lossless packet
-- From STATE_DB.ASIC_TABLE:
--      cell size:                  cell_size of the ASIC
--      pipeline_latency:           the latency (XON)
--      mac_phy_delay:              the bytes held in the switch chip's egress pipeline and PHY when XOFF has been generated
--      peer_response_time:         the bytes that are held in the peer switch's pipeline and will be send out when the XOFF packet is received

local lossless_mtu
local small_packet_percentage
local cell_size
local pipeline_latency
local mac_phy_delay
local peer_response_time

local port_speed = tonumber(ARGV[1])
local cable_length = tonumber(string.sub(ARGV[2], 1, -2))
local port_mtu = tonumber(ARGV[3])
local gearbox_delay = tonumber(ARGV[4])

local config_db = "4"
local state_db = "6"

local ret = {}

-- Pause quanta should be taken for each operating speed is defined in IEEE 802.3 31B.3.7.
-- The key of table pause_quanta_per_speed is operating speed at Mb/s.
-- The value of table pause_quanta_per_speed is the number of pause_quanta.
local pause_quanta_per_speed = {}
pause_quanta_per_speed[400000] = 905
pause_quanta_per_speed[200000] = 453
pause_quanta_per_speed[100000] = 394
pause_quanta_per_speed[50000] = 147
pause_quanta_per_speed[40000] = 118
pause_quanta_per_speed[25000] = 80
pause_quanta_per_speed[10000] = 67
pause_quanta_per_speed[1000] = 2
pause_quanta_per_speed[100] = 1

-- Get pause_quanta from the pause_quanta_per_speed table
local pause_quanta = pause_quanta_per_speed[port_speed]

if gearbox_delay == nil then
    gearbox_delay = 0
end

-- Fetch ASIC info from ASIC table in STATE_DB
redis.call("SELECT", state_db)
local asic_keys = redis.call("KEYS", "ASIC_TABLE*")

-- Only one key should exist
local asic_table_content = redis.call("HGETALL", asic_keys[1])

for i = 1, #asic_table_content, 2 do
    if asic_table_content[i] == "cell_size" then
        cell_size = tonumber(asic_table_content[i+1])
    end
    if asic_table_content[i] == "pipeline_latency" then
        pipeline_latency = tonumber(asic_table_content[i+1]) * 1024
    end
    if asic_table_content[i] == "mac_phy_delay" then
        mac_phy_delay = tonumber(asic_table_content[i+1]) * 1024
    end
    -- If failed to get pause_quanta from the table, then use the default peer_response_time stored in state_db
    if asic_table_content[i] == "peer_response_time" and  pause_quanta == nil then
        peer_response_time = tonumber(asic_table_content[i+1]) * 1024
    end
end

-- Fetch lossless traffic info from CONFIG_DB
redis.call("SELECT", config_db)
local lossless_traffic_keys = redis.call("KEYS", "LOSSLESS_TRAFFIC_PATTERN*")

-- Only one key should exist
local lossless_traffic_table_content = redis.call("HGETALL", lossless_traffic_keys[1])
for i = 1, #lossless_traffic_table_content, 2 do
    if lossless_traffic_table_content[i] == "mtu" then
        lossless_mtu = tonumber(lossless_traffic_table_content[i+1])
    end
    if lossless_traffic_table_content[i] == "small_packet_percentage" then
        small_packet_percentage = tonumber(lossless_traffic_table_content[i+1])
    end
end

-- Fetch the shared headroom pool size
local shp_size = tonumber(redis.call("HGET", "BUFFER_POOL|ingress_lossless_pool", "xoff"))

-- Calculate the headroom information
local speed_of_light = 198000000
local minimal_packet_size = 64
local cell_occupancy
local worst_case_factor
local propagation_delay
local bytes_on_cable
local bytes_on_gearbox
local xoff_value
local xon_value
local headroom_size

if cell_size > 2 * minimal_packet_size then
    worst_case_factor = cell_size / minimal_packet_size
else
    worst_case_factor = (2 * cell_size) / (1 + cell_size)
end

cell_occupancy = (100 - small_packet_percentage + small_packet_percentage * worst_case_factor) / 100

if (gearbox_delay == 0) then
    bytes_on_gearbox = 0
else
    bytes_on_gearbox = port_speed * gearbox_delay / (8 * 1024)
end

-- If successfully get pause_quanta from the table, then calculate peer_response_time from it
if pause_quanta ~= nil then
    peer_response_time = (pause_quanta) * 512 / 8
end

if port_speed == 400000 then
    peer_response_time = 2 * peer_response_time
end

bytes_on_cable = 2 * cable_length * port_speed * 1000000000 / speed_of_light / (8 * 1024)
propagation_delay = port_mtu + bytes_on_cable + 2 * bytes_on_gearbox + mac_phy_delay + peer_response_time

-- Calculate the xoff and xon and then round up at 1024 bytes
xoff_value = lossless_mtu + propagation_delay * cell_occupancy
xoff_value = math.ceil(xoff_value / 1024) * 1024
xon_value = pipeline_latency
xon_value = math.ceil(xon_value / 1024) * 1024

headroom_size = xon_value
headroom_size = math.ceil(headroom_size / 1024) * 1024

table.insert(ret, "xon" .. ":" .. math.ceil(xon_value))
table.insert(ret, "xoff" .. ":" .. math.ceil(xoff_value))
table.insert(ret, "size" .. ":" .. math.ceil(headroom_size))

return ret

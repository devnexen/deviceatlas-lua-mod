dalua = require("dalua")
if #arg < 2 then
  print "Needs the path of the json file and the user-agent"
  os.exit()
end
cfg = {}
cfg["cache_size"] = 10000
d = dalua.new()
d:load_data_from_file(arg[1])
print(d)
start = os.clock()
dt = 0
for i=0, 100000 do
    t = d:get_properties(arg[2])
    dt = dt + 1
end
send = os.clock()
for k, v in pairs(t) do
    print(k, v)
end

print("Elapsed time for ", dt, " detections :", (send - start))


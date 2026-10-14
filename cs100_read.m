% Trigger a CS-100 measurement through the Arduino and read back Y, x, y.
port = "/dev/cu.wchusbserial1140";

s = serialport(port, 115200, "Timeout", 10);
try
    configureTerminator(s, "LF");
    pause(2.5);  % covers a board that resets when the port opens
    flush(s);

    writeline(s, "?");
    reply = strtrim(readline(s));
    assert(reply == "OK,CS100", "Unexpected handshake reply: %s", reply);

    writeline(s, "M");
    reply = strtrim(readline(s));
    r = split(reply, ",");
    if r(1) ~= "OK"
        error("CS-100 read failed: %s", reply);
    end
    Yxy = str2double(r(2:4))';  % [Y x y]
    fprintf("Y = %g cd/m^2, x = %.4f, y = %.4f\n", Yxy);
catch err
    clear s  % release the port before reporting the failure
    rethrow(err);
end
clear s

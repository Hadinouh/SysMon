using System.Globalization;
using LibreHardwareMonitor.Hardware;

class Program
{
    static readonly Computer computer = new Computer
    {
        IsCpuEnabled = true,
        IsMotherboardEnabled = true
    };

    static void Main()
    {
        using CancellationTokenSource cancellation =
            new CancellationTokenSource();

        Console.CancelKeyPress += (sender, e) =>
        {
            e.Cancel = true;
            cancellation.Cancel();
        };

        try
        {
            computer.Open();
            while (!cancellation.IsCancellationRequested)
            {
                Console.WriteLine("SNAPSHOT_BEGIN");

                foreach (IHardware hardware in computer.Hardware)
                {
                    try { ReadHardware(hardware); }
                    catch (Exception) { Console.WriteLine("ERROR|Hardware sensor unavailable"); }
                }

                Console.WriteLine("SNAPSHOT_END");
                Console.Out.Flush();

                if (cancellation.Token.WaitHandle.WaitOne(500))
                    break;
            }
        }
        catch (IOException) { /* Parent closed the pipe. */ }
        catch (Exception) { try { Console.Error.WriteLine("ERROR|Sensor initialization failed"); } catch { } }
        finally
        {
            try { computer.Close(); } catch { }
        }
    }

    static void ReadHardware(IHardware hardware)
    {
        hardware.Update();

        foreach (ISensor sensor in hardware.Sensors)
        {
            if (
                sensor.SensorType != SensorType.Temperature &&
                sensor.SensorType != SensorType.Power &&
                sensor.SensorType != SensorType.Clock &&
                sensor.SensorType != SensorType.Load &&
                sensor.SensorType != SensorType.Fan
            )
            {
                continue;
            }

            string value = "--";

            if (sensor.Value.HasValue)
            {
                value = sensor.Value.Value.ToString(
                    "0.##",
                    CultureInfo.InvariantCulture
                );
            }

            Console.WriteLine(
                "SENSOR|" +
                Clean(hardware.HardwareType.ToString()) + "|" +
                Clean(hardware.Name) + "|" +
                Clean(sensor.SensorType.ToString()) + "|" +
                Clean(sensor.Name) + "|" +
                value
            );
        }

        foreach (IHardware subHardware in hardware.SubHardware)
        {
            ReadHardware(subHardware);
        }
    }

    static string Clean(string value)
    {
        return value
            .Replace("|", "/")
            .Replace("\r", " ")
            .Replace("\n", " ");
    }
}

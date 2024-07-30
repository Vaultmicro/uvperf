# Get a list of all USB devices
$usbDevices = Get-WmiObject -Query "SELECT * FROM Win32_PnPEntity WHERE PNPDeviceID LIKE 'USB%'"

# Iterate over each USB device
$usbDevices | ForEach-Object {
    $device = $_
    $deviceIDParts = $device.PNPDeviceID -split '\\'
    $vendorID = $null
    $productID = $null

    foreach ($part in $deviceIDParts) {
        if ($part -match "^VID_([0-9A-F]{4})") {
            $vendorID = $matches[1]
        }
        if ($part -match "PID_([0-9A-F]{4})") {
            $productID = $matches[1]
        }
    }

    $serial = $deviceIDParts[2] -split '#' | Select-Object -First 1

    [PSCustomObject]@{
        Name = $device.Name
        VID = if ($vendorID) { $vendorID } else { "N/A" }
        PID = if ($productID) { $productID } else { "N/A" }
        SerialNumber = $serial
    }
} | Format-Table -AutoSize

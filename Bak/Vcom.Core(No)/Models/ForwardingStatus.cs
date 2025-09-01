namespace VCom.Core.Models
{
    /// <summary>
    /// Represents the current operational status of a data forwarder.
    /// </summary>
    public enum ForwardingStatus
    {
        Idle,
        Running,
        Connected,
        Error
    }
}
/** @file
  Lightweight lib to support EFI Server Management drivers.

Copyright (c) 2023, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiDxe.h>
#include <Library/IoLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GenericElog.h>

#include <Library/ServerMgmtRtLib.h>

// #include EFI_PROTOCOL_DEPENDENCY (CpuIo)

#define PCAT_RTC_ADDRESS_REGISTER  0x70
#define PCAT_RTC_DATA_REGISTER     0x71

#define RTC_ADDRESS_SECONDS           0   // R/W  Range 0..59
#define RTC_ADDRESS_MINUTES           2   // R/W  Range 0..59
#define RTC_ADDRESS_HOURS             4   // R/W  Range 1..12 or 0..23 Bit 7 is AM/PM
#define RTC_ADDRESS_DAY_OF_THE_MONTH  7   // R/W  Range 1..31
#define RTC_ADDRESS_MONTH             8   // R/W  Range 1..12
#define RTC_ADDRESS_YEAR              9   // R/W  Range 0..99
#define RTC_ADDRESS_REGISTER_A        10  // R/W[0..6]  R0[7]
#define RTC_ADDRESS_REGISTER_B        11  // R/W
#define RTC_ADDRESS_REGISTER_C        12  // RO
#define RTC_ADDRESS_REGISTER_D        13  // RO
#define RTC_ADDRESS_CENTURY           50  // R/W  Range 19..20 Bit 8 is R/W

//
// Register A
//
typedef struct {
  UINT8    RS  : 4; // Rate Selection Bits
  UINT8    DV  : 3; // Divisor
  UINT8    UIP : 1; // Update in progress
} RTC_REGISTER_A_BITS;

typedef union {
  RTC_REGISTER_A_BITS    Bits;
  UINT8                  Data;
} RTC_REGISTER_A;

//
// Register B
//
typedef struct {
  UINT8    DSE  : 1; // 0 - Daylight saving disabled  1 - Daylight savings enabled
  UINT8    MIL  : 1; // 0 - 12 hour mode              1 - 24 hour mode
  UINT8    DM   : 1; // 0 - BCD Format                1 - Binary Format
  UINT8    SQWE : 1; // 0 - Disable SQWE output       1 - Enable SQWE output
  UINT8    UIE  : 1; // 0 - Update INT disabled       1 - Update INT enabled
  UINT8    AIE  : 1; // 0 - Alarm INT disabled        1 - Alarm INT Enabled
  UINT8    PIE  : 1; // 0 - Periodic INT disabled     1 - Periodic INT Enabled
  UINT8    SET  : 1; // 0 - Normal operation.         1 - Updates inhibited
} RTC_REGISTER_B_BITS;

typedef union {
  RTC_REGISTER_B_BITS    Bits;
  UINT8                  Data;
} RTC_REGISTER_B;

//
// Register D
//
typedef struct {
  UINT8    Reserved : 7; // Read as zero.  Can not be written.
  UINT8    VRT      : 1; // Valid RAM and Time
} RTC_REGISTER_D_BITS;

typedef union {
  RTC_REGISTER_D_BITS    Bits;
  UINT8                  Data;
} RTC_REGISTER_D;

//
// Module Globals
//
EFI_SM_ELOG_PROTOCOL  *mGenericElogProtocol     = NULL;
VOID                  *mGenericElogRegistration = NULL;

INTN  DaysOfMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

/**
  This function is called whenever an instance of ELOG protocol is created.  When the function is notified
  it initializes the module global data.

  @param Event                  This is used only for EFI compatibility.
  @param Context                This is used only for EFI compatibility.

**/
VOID
EFIAPI
GenericElogNotificationFunction (
  IN  EFI_EVENT  Event,
  IN  VOID       *Context
  )
{
  EFI_STATUS  Status;

  Status = gBS->LocateProtocol (&gEfiGenericElogProtocolGuid, NULL, (VOID **)&mGenericElogProtocol);
  if (EFI_ERROR (Status)) {
    mGenericElogProtocol = NULL;
  }
}

/**
  The function will set up a notification on the ELOG protocol.  This function is required to be called prior
  to utilizing the ELOG protocol from within this library.

  @retval EFI_SUCCESS          after the notification has been setup.
**/
EFI_STATUS
EfiInitializeGenericElog (
  VOID
  )
{
  EFI_EVENT   Event;
  EFI_STATUS  Status;

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, GenericElogNotificationFunction, NULL, &Event);
  ASSERT_EFI_ERROR (Status);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  Status = gBS->RegisterProtocolNotify (&gEfiGenericElogProtocolGuid, Event, &mGenericElogRegistration);
  ASSERT_EFI_ERROR (Status);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  gBS->SignalEvent (Event);

  return EFI_SUCCESS;
}

/**
  This function sends event log data to the destination such as LAN, ICMB, BMC etc.

  @param ElogData     is a pointer to the event log data that needs to be recorded
  @param DataType     type of Elog data that is being recorded.  The Elog is redirected based on this parameter.
  @param AlertEvent   is an indication that the input data type is an alert.  The underlying
                      drivers need to decide if they need to listen to the DataType and send it on
                      an appropriate channel as an alert use of the information.
  @param DataSize     is the size of the data to be logged
  @param RecordId     is the array of record IDs sent by the target.  This can be used to retrieve the
                      records or erase the records.
  @retval EFI_SUCCESS - if the data was logged.
  @retval EFI_INVALID_PARAMETER - if the DataType is >= EfiSmElogMax
  @retval EFI_NOT_FOUND - the event log target was not found
  @retval EFI_PROTOCOL_ERROR - there was a data formatting error

**/
EFI_STATUS
EfiSmSetEventLogData (
  IN  UINT8             *ElogData,
  IN  EFI_SM_ELOG_TYPE  DataType,
  IN  BOOLEAN           AlertEvent,
  IN  UINTN             DataSize,
  OUT UINT64            *RecordId
  )
{
  //
  // If the protocol is not found return EFI_NOT_FOUND
  //
  if (mGenericElogProtocol == NULL) {
    return EFI_NOT_FOUND;
  }

  return mGenericElogProtocol->SetEventLogData (
                                 mGenericElogProtocol,
                                 ElogData,
                                 DataType,
                                 AlertEvent,
                                 DataSize,
                                 RecordId
                                 );
}

/**
  This function gets event log data from the destination dependent on the DataType.  The destination
  can be a remote target such as LAN, ICMB, IPMI, or a FV.  The ELOG redir driver will resolve the
  destination.

  @param ElogData - pointer to the event log data buffer to contain the data to be retrieved.
  @param DataType - this is the type of Elog data to be gotten.  Elog is redirected based upon this
                    information.
  @param DataSize - this is the size of the data to be retrieved.
  @param RecordId - the RecordId of the next record.  If ElogData is NULL, this gives the RecordId of the first
                    record available in the database with the correct DataSize.  A value of 0 on return indicates
                    that it was last record if the Status is EFI_SUCCESS.

  @retval EFI_SUCCESS - if the event log was retrieved successfully.
  @retval EFI_NOT_FOUND - if the event log target was not found.
  @retval EFI_NO_RESPONSE - if the event log target is not responding.  This is done by the redir driver.
  @retval EFI_INVALID_PARAMETER - DataType or another parameter was invalid.
  @retval EFI_BUFFER_TOO_SMALL -the ElogData buffer is too small to be filled with the requested data.

**/
EFI_STATUS
EfiSmGetEventLogData (
  IN  UINT8             *ElogData,
  IN  EFI_SM_ELOG_TYPE  DataType,
  IN  OUT UINTN         *DataSize,
  IN OUT UINT64         *RecordId
  )
{
  //
  // If the protocol is not found return EFI_NOT_FOUND
  //
  if (mGenericElogProtocol == NULL) {
    return EFI_NOT_FOUND;
  }

  return mGenericElogProtocol->GetEventLogData (
                                 mGenericElogProtocol,
                                 ElogData,
                                 DataType,
                                 DataSize,
                                 RecordId
                                 );
}

/**
  This function erases the event log data defined by the DataType.  The redir driver associated with
  the DataType resolves the path to the record.

  @param DataType - the type of Elog data that is to be erased.
  @param RecordId - the RecordId of the data to be erased.  If RecordId is NULL, all records in the
                    database are erased if permitted by the target.  RecordId will contain the deleted
                    RecordId on return.

  @retval EFI_SUCCESS - the record or collection of records were erased.
  @retval EFI_NOT_FOUND - the event log target was not found.
  @retval EFI_NO_RESPONSE - the event log target was found but did not respond.
  @retval EFI_INVALID_PARAMETER - one of the parameters was invalid.

**/
EFI_STATUS
EfiSmEraseEventlogData (
  IN EFI_SM_ELOG_TYPE  DataType,
  IN OUT UINT64        *RecordId
  )
{
  //
  // If the protocol is not found return EFI_NOT_FOUND
  //
  if (mGenericElogProtocol == NULL) {
    return EFI_NOT_FOUND;
  }

  return mGenericElogProtocol->EraseEventlogData (
                                 mGenericElogProtocol,
                                 DataType,
                                 RecordId
                                 );
}

/**
  This function enables or disables the event log defined by the DataType.


  @param DataType   - the type of Elog data that is being activated.
  @param EnableElog - enables or disables the event log defined by the DataType.  If it is NULL
                      it returns the current status of the DataType log.
  @param ElogStatus - is the current status of the Event log defined by the DataType.  Enabled is
                      TRUE and Disabled is FALSE.

  @retval EFI_SUCCESS - if the event log was successfully enabled or disabled.
  @retval EFI_NOT_FOUND - the event log target was not found.
  @retval EFI_NO_RESPONSE - the event log target was found but did not respond.
  @retval EFI_INVALID_PARAMETER - one of the parameters was invalid.

**/
EFI_STATUS
EfiSmActivateEventLog (
  IN EFI_SM_ELOG_TYPE  DataType,
  IN BOOLEAN           *EnableElog,
  OUT BOOLEAN          *ElogStatus
  )
{
  //
  // If the protocol is not found return EFI_NOT_FOUND
  //
  if (mGenericElogProtocol == NULL) {
    return EFI_NOT_FOUND;
  }

  return mGenericElogProtocol->ActivateEventLog (
                                 mGenericElogProtocol,
                                 DataType,
                                 EnableElog,
                                 ElogStatus
                                 );
}

/**
  This function verifies the leap year

  @param  Year    year in YYYY format.

  @retval TRUE if the year is a leap year

**/
STATIC
BOOLEAN
IsLeapYear (
  IN UINT16  Year
  )
{
  if (Year % 4 == 0) {
    if (Year % 100 == 0) {
      if (Year % 400 == 0) {
        return TRUE;
      } else {
        return FALSE;
      }
    } else {
      return TRUE;
    }
  } else {
    return FALSE;
  }
}

/**
  This function calculates the total number leap days from 1970 to the current year

  @param  Time    - Current Time

  @retval Returns the number of leap days since the base year, 1970.

**/
STATIC
UINTN
CountNumOfLeapDays (
  IN EFI_TIME  *Time
  )
{
  UINT16  NumOfYear;
  UINT16  BaseYear;
  UINT16  Index;
  UINTN   Count;

  Count     = 0;
  BaseYear  = 1970;
  NumOfYear = Time->Year - 1970;

  for (Index = 0; Index <= NumOfYear; Index++) {
    if (IsLeapYear (BaseYear + Index)) {
      Count++;
    }
  }

  //
  // If the current year is a leap year but the month is January or February,
  // then the leap day has not occurred and should not be counted. If it is
  // February 29, the leap day is accounted for in CalculateNumOfDayPassedThisYear( )
  //
  if (IsLeapYear (Time->Year)) {
    if ((Count > 0) && (Time->Month < 3)) {
      Count--;
    }
  }

  return Count;
}

/**
  This function calculates the total number of days passed till the day in a year.
  If the year is a leap year, an extra day is not added since the number of leap
  days is calculated in CountNumOfLeapDays.

  @param  Time    This structure contains detailed information about date and time

  @retval Returns the number of days passed until the input day.

**/
STATIC
UINTN
CalculateNumOfDayPassedThisYear (
  IN  EFI_TIME  Time
  )
{
  UINTN  Index;
  UINTN  NumOfDays;

  NumOfDays = 0;
  for (Index = 1; Index < Time.Month; Index++) {
    NumOfDays += DaysOfMonth[Index - 1];
  }

  NumOfDays += Time.Day;
  return NumOfDays;
}

/**
  Function converts a BCD to a decimal value.

  @param[in] BcdValue   An 8 bit BCD value

  @return The decimal value of the BcdValue
**/
STATIC
UINT8
BcdToDecimal (
  IN  UINT8  BcdValue
  )
{
  UINTN  High;
  UINTN  Low;

  High = BcdValue >> 4;
  Low  = BcdValue - (High << 4);

  return (UINT8)(Low + (High * 10));
}

//
// RTC read functions were copied here since we need to get the time
// in both DXE and runtime code.  The PcRtc driver is not currently a
// dual mode driver, this is more efficient since making PcRtc dual mode
// would unnecessarily bloat the SMM code space.
//

/**
  Read data register and return contents.

  @param Address - Register address to read

  @retval Value of data register contents

**/
STATIC
UINT8
RtcRead (
  IN  UINT8  Address
  )
{
  IoWrite8 (PCAT_RTC_ADDRESS_REGISTER, (UINT8)(Address | (UINT8)(IoRead8 (PCAT_RTC_ADDRESS_REGISTER) & 0x80)));
  return IoRead8 (PCAT_RTC_DATA_REGISTER);
}

/**
  Write data to register address.

  @param Address - Register address to write
  @param Data   - Data to write to register

**/
STATIC
VOID
RtcWrite (
  IN  UINT8  Address,
  IN  UINT8  Data
  )
{
  IoWrite8 (PCAT_RTC_ADDRESS_REGISTER, (UINT8)(Address | (UINT8)(IoRead8 (PCAT_RTC_ADDRESS_REGISTER) & 0x80)));
  IoWrite8 (PCAT_RTC_DATA_REGISTER, Data);
}

/**
  Convert Rtc Time To Efi Time.

  @param Time
  @param RegisterB

**/
STATIC
VOID
ConvertRtcTimeToEfiTime (
  IN EFI_TIME        *Time,
  IN RTC_REGISTER_B  RegisterB
  )
{
  BOOLEAN  Pm;

  if ((Time->Hour) & 0x80) {
    Pm = TRUE;
  } else {
    Pm = FALSE;
  }

  Time->Hour = (UINT8)(Time->Hour & 0x7f);

  if (RegisterB.Bits.DM == 0) {
    Time->Year   = BcdToDecimal ((UINT8)Time->Year);
    Time->Month  = BcdToDecimal (Time->Month);
    Time->Day    = BcdToDecimal (Time->Day);
    Time->Hour   = BcdToDecimal (Time->Hour);
    Time->Minute = BcdToDecimal (Time->Minute);
    Time->Second = BcdToDecimal (Time->Second);
  }

  //
  // If time is in 12 hour format, convert it to 24 hour format
  //
  if (RegisterB.Bits.MIL == 0) {
    if (Pm && (Time->Hour < 12)) {
      Time->Hour = (UINT8)(Time->Hour + 12);
    }

    if (!Pm && (Time->Hour == 12)) {
      Time->Hour = 0;
    }
  }

  Time->Nanosecond = 0;
  Time->TimeZone   = EFI_UNSPECIFIED_TIMEZONE;
  Time->Daylight   = 0;
}

/**
  Test Century Register.

  @retval EFI_SUCCESS
  @retval EFI_DEVICE_ERROR

**/
STATIC
EFI_STATUS
RtcTestCenturyRegister (
  VOID
  )
{
  UINT8  Century;
  UINT8  Temp;

  Century = RtcRead (RTC_ADDRESS_CENTURY);

  //
  // Always sync-up the Bit7 "semaphore"...this maintains
  // consistency across the different chips/implementations of
  // the RTC...
  //
  RtcWrite (RTC_ADDRESS_CENTURY, 0x00);
  Temp = (UINT8)(RtcRead (RTC_ADDRESS_CENTURY) & 0x7f);
  RtcWrite (RTC_ADDRESS_CENTURY, Century);
  if ((Temp == 0x19) || (Temp == 0x20)) {
    return EFI_SUCCESS;
  }

  return EFI_DEVICE_ERROR;
}

/**
  Waits until RTC register A and D show data is valid.

  @param Timeout - Maximum time to wait

  @retval EFI_DEVICE_ERROR
  @retval EFI_SUCCESS

**/
STATIC
EFI_STATUS
RtcWaitToUpdate (
  UINTN  Timeout
  )
{
  RTC_REGISTER_A  RegisterA;
  RTC_REGISTER_D  RegisterD;

  //
  // See if the RTC is functioning correctly
  //
  RegisterD.Data = RtcRead (RTC_ADDRESS_REGISTER_D);

  if (RegisterD.Bits.VRT == 0) {
    return EFI_DEVICE_ERROR;
  }

  //
  // Wait for up to 0.1 seconds for the RTC to be ready.
  //
  Timeout        = (Timeout / 10) + 1;
  RegisterA.Data = RtcRead (RTC_ADDRESS_REGISTER_A);
  while (RegisterA.Bits.UIP == 1 && Timeout > 0) {
    gBS->Stall (10);
    RegisterA.Data = RtcRead (RTC_ADDRESS_REGISTER_A);
    Timeout--;
  }

  RegisterD.Data = RtcRead (RTC_ADDRESS_REGISTER_D);
  if ((Timeout == 0) || (RegisterD.Bits.VRT == 0)) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

/**
  Get time from RTC.

  @param Time - pointer to time structure

  @retval EFI_INVALID_PARAMETER
  @retval EFI_SUCCESS

**/
STATIC
EFI_STATUS
RtcGetTime (
  OUT EFI_TIME  *Time
  )
{
  RTC_REGISTER_B  RegisterB;
  UINT8           Century;
  EFI_STATUS      Status;

  //
  // Check parameters for null pointer
  //
  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Wait for up to 0.1 seconds for the RTC to be updated
  //
  Status = RtcWaitToUpdate (100000);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Read Register B
  //
  RegisterB.Data = RtcRead (RTC_ADDRESS_REGISTER_B);

  //
  // Get the Time/Date/Daylight Savings values.
  //
  Time->Second = RtcRead (RTC_ADDRESS_SECONDS);
  Time->Minute = RtcRead (RTC_ADDRESS_MINUTES);
  Time->Hour   = RtcRead (RTC_ADDRESS_HOURS);
  Time->Day    = RtcRead (RTC_ADDRESS_DAY_OF_THE_MONTH);
  Time->Month  = RtcRead (RTC_ADDRESS_MONTH);
  Time->Year   = RtcRead (RTC_ADDRESS_YEAR);

  ConvertRtcTimeToEfiTime (Time, RegisterB);

  if (RtcTestCenturyRegister () == EFI_SUCCESS) {
    Century = BcdToDecimal ((UINT8)(RtcRead (RTC_ADDRESS_CENTURY) & 0x7f));
  } else {
    Century = BcdToDecimal (RtcRead (RTC_ADDRESS_CENTURY));
  }

  Time->Year = (UINT16)(Century * 100 + Time->Year);

  return EFI_SUCCESS;
}

/**
  Return Date and Time from RTC in Unix format which fits in 32 bit format.

  @param NumOfSeconds - pointer to return calculated time

  @retval EFI_SUCCESS
  @retval EFI status if error occurred

**/
EFI_STATUS
EfiSmGetTimeStamp (
  OUT UINT32  *NumOfSeconds
  )
{
  UINT16      NumOfYears;
  UINTN       NumOfLeapDays;
  UINTN       NumOfDays;
  EFI_TIME    Time;
  EFI_STATUS  Status;

  Status = RtcGetTime (&Time);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  NumOfYears    = Time.Year - 1970;
  NumOfLeapDays = CountNumOfLeapDays (&Time);
  NumOfDays     = CalculateNumOfDayPassedThisYear (Time);

  //
  // Add 365 days for all years. Add additional days for Leap Years. Subtract off current day.
  //
  NumOfDays += (NumOfLeapDays + (365 * NumOfYears) - 1);

  *NumOfSeconds = (UINT32)(3600 * 24 * NumOfDays + (Time.Hour * 3600) + (60 * Time.Minute) + Time.Second);

  return EFI_SUCCESS;
}

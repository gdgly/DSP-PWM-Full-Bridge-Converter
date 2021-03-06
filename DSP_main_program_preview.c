//#####################################################################
//  Author: Hamid R. Tanhaei
//  Chip: TMS320F28033
//  clock Freq: 60MHz (input osc. freq.:12MHz)
//  Synthesizing PWM signals for a full-bridge Mosfet
//  measuring the power consumption of bridge under load through current sensing
//  current sensing is carried out via a resistor in which the current flows
//  and measuring the voltage across the resistor through ADC facility in DSP
//  based on measured value, controlling the PWM gap time to regulate the power consumption
//#####################################################################
#include "DSP28x_Project.h"     // Device Headerfile and Examples Include File
#include <stdio.h>
//
#define Bridge_En_Mux       GpioCtrlRegs.GPAMUX1.bit.GPIO1
#define Bridge_En_Dir       GpioCtrlRegs.GPADIR.bit.GPIO1
#define Bridge_En_Clr       GpioDataRegs.GPACLEAR.bit.GPIO1
#define Bridge_En_Set       GpioDataRegs.GPASET.bit.GPIO1
#define Bridge_En_Dat       GpioDataRegs.GPADAT.bit.GPIO1
//
#define PWM_Sig_1_Mux       GpioCtrlRegs.GPAMUX1.bit.GPIO2  // EPWM2A
#define PWM_Sig_1_Dir       GpioCtrlRegs.GPADIR.bit.GPIO2
#define PWM_Sig_1_Clr       GpioDataRegs.GPACLEAR.bit.GPIO2
#define PWM_Sig_1_Set       GpioDataRegs.GPASET.bit.GPIO2
#define PWM_Sig_1_Dat       GpioDataRegs.GPADAT.bit.GPIO2
//
#define PWM_Sig_2_Mux       GpioCtrlRegs.GPAMUX1.bit.GPIO3  // EPWM2B
#define PWM_Sig_2_Dir       GpioCtrlRegs.GPADIR.bit.GPIO3
#define PWM_Sig_2_Clr       GpioDataRegs.GPACLEAR.bit.GPIO3
#define PWM_Sig_2_Set       GpioDataRegs.GPASET.bit.GPIO3
#define PWM_Sig_2_Dat       GpioDataRegs.GPADAT.bit.GPIO3
//
#define     PWM_Period   500  // (30000/60KHz)
#define     short_circuit_threshold 250
#define     Max_Gap  100 //Min power
#define     Min_Gap  300 //Max power
#define     Pwm_Pwr_AGC_Threshold   1400
//
void Initialize_Routine(void);
__interrupt void cpu_timer0_isr(void);
void Init_GPIOs(void);
void Init_ADC(void);
void Init_PWM_Signals(void);
void Update_PWM_Freq(Uint16 Period, Uint16 Gap);
void Disable_PWM_Signal(void);
void ADC_Bridge_Power(void);
void AGC_Bridge_Power(void);
void ADC_Avrager(Uint16 state);
void Critical_Checks(void);
void Reset_Device(void);
//
Uint16 ADC_BridgePwr_Buffer[4], ADC_Bridge_zero_offset[4];
Uint16 Bridge_pwr_instant, Bridge_pwr_Value;
Uint16 Avg_counter, Pwr_offset;
Uint16 PwmGap = Max_Gap;
Uint16 Bridge_pwr_period = PWM_Period;
//=================================//
void main(void)
{
    Init_GPIOs();
    //
    memcpy(&RamfuncsRunStart, &RamfuncsLoadStart, (size_t) &RamfuncsLoadSize);
    //
    InitSysCtrl(); // Step 1. Initialize System Control: PLL, WatchDog, enable Peripheral Clocks
    //
    DINT;
    // Disable CPU interrupt
    //
    InitFlash();
    //
    InitPieCtrl(); // Initialize the PIE control registers to their default state. The default state is all PIE interrupts disabled and flags are cleared.
    //
    IER = 0x0000;  // Disable CPU interrupts and clear all CPU interrupt flags:
    IFR = 0x0000;
    // Initialize the PIE vector table with pointers to the shell Interrupt
    // Service Routines (ISR).
    // This will populate the entire table, even if the interrupt
    // is not used in this example.  This is useful for debug purposes.
    // The shell ISR routines are found in DSP2803x_DefaultIsr.c.
    InitPieVectTable();
    //
    // Interrupts that are used in this example are re-mapped to
    // ISR functions found within this file.
    //
    Init_GPIOs();
    Init_ADC();
    Initialize_Routine();
    Init_PWM_Signals();
    ServiceDog();    // Reset the watchdog counter
    //
    // Enable Timer_10ms:
    CpuTimer0Regs.TIM.all = 0;
    CpuTimer0Regs.PRD.all = (Uint32) (600000);  //10msec
    CpuTimer0Regs.TCR.bit.TRB = 1; // Reload timer0
    CpuTimer0Regs.TCR.bit.TSS = 0; // Start Timer0
    CpuTimer0Regs.TCR.bit.TIE = 1; // Enable timer interrupt
    //
    EINT;
    // Enable Global interrupt INTM
    ERTM;
    // Enable Global realtime interrupt DBGM
    //
    for (;;);
}
//=================================//
void Initialize_Routine(void)
{
    // Enable Watchdog
    ServiceDog();    // Reset the watchdog counter
    EALLOW;
    SysCtrlRegs.SCSR = 0; //BIT1;
    SysCtrlRegs.WDCR = 0x002F; // WDCLK = OSCCLK/512/64
    //
    DINT;
    InitCpuTimers();   // For this example, only initialize the Cpu Timers
    CpuTimer0Regs.TCR.all = 0; // Use write-only instruction
    CpuTimer0Regs.TCR.bit.TIE = 0;
    PieVectTable.TINT0 = &cpu_timer0_isr;
    EDIS;
    // This is needed to disable write to EALLOW protected registers
    // Enable TINT0 in the PIE: Group 1 interrupt
    PieCtrlRegs.PIECTRL.bit.ENPIE = 1;    // Enable the PIE block
    PieCtrlRegs.PIEIER1.bit.INTx7 = 1; // TINT0 (Timer0)   // Enable PIE Group 1 INT7
    IER |= M_INT1;                        // Enable CPU INT1
    DINT;
}
//============================================//
#pragma CODE_SECTION(Init_ADC, "ramfuncs");
void Init_ADC(void)
{
    EALLOW;
    // initialize adc
    SysCtrlRegs.PCLKCR0.bit.ADCENCLK = 1; // Return ADC clock to original state
    __asm (" NOP");
    __asm (" NOP");
    AdcRegs.ADCCTL1.bit.RESET = 1;
    __asm (" NOP");
    __asm (" NOP");
    __asm (" NOP");
    __asm (" NOP");
    AdcRegs.ADCCTL1.bit.ADCPWDN = 1;
    AdcRegs.ADCCTL1.bit.ADCBGPWD = 1;
    AdcRegs.ADCCTL1.bit.ADCREFPWD = 1;
    AdcRegs.ADCCTL1.bit.ADCREFSEL = 1;
    AdcRegs.ADCCTL1.bit.INTPULSEPOS = 1; //0;
    AdcRegs.ADCCTL1.bit.VREFLOCONV = 0;
    AdcRegs.ADCCTL1.bit.TEMPCONV = 0;
    //
    AdcRegs.ADCCTL2.bit.CLKDIV2EN = 0;
    AdcRegs.ADCCTL2.bit.ADCNONOVERLAP = 0;
    //
    AdcRegs.ADCSAMPLEMODE.bit.SIMULEN0 = 1;
    //
    AdcRegs.ADCSOC2CTL.bit.CHSEL = 0x3; // A3 pin for Bridge power
    AdcRegs.ADCSOC2CTL.bit.ACQPS = 63;
    AdcRegs.ADCSOC2CTL.bit.TRIGSEL = 0; // software trigger
    AdcRegs.ADCINTSOCSEL1.bit.SOC2 = 0;
    AdcRegs.INTSEL3N4.bit.INT3CONT = 0;
    AdcRegs.INTSEL3N4.bit.INT3SEL = 2;  // connect EOC2 to ADCINT3
    AdcRegs.INTSEL3N4.bit.INT3E = 1;
    //
    AdcRegs.ADCCTL1.bit.ADCENABLE = 1;
    DELAY_US(1000);
    EDIS;
}
//================================================//
void Init_GPIOs(void)
{
    // Configure GPIOs:
    EALLOW;
    ///////
    Bridge_En_Mux = 0;
    Bridge_En_Dir = 1;
    Bridge_En_Set = 1;
    //
    PWM_Sig_1_Mux = 0;
    PWM_Sig_1_Dir = 1;
    PWM_Sig_1_Clr = 1;
    //
    PWM_Sig_2_Mux = 0;
    PWM_Sig_2_Dir = 1;
    PWM_Sig_2_Clr = 1;
    //
    EDIS;
}
//==============================================//
#pragma CODE_SECTION(Init_PWM_Signals, "ramfuncs");
void Init_PWM_Signals(void)
{
    EALLOW;
    //
    EPwm2Regs.TBPRD = 500; // Period = * TBCLK counts
    EPwm2Regs.CMPA.half.CMPA = 300; // Compare A TBCLK counts
    EPwm2Regs.CMPB = 200; // Compare B TBCLK counts
    EPwm2Regs.TBPHS.half.TBPHS = 0; // Set Phase register to zero
    EPwm2Regs.TBPHS.all = 0; // Set Phase register to zero
    EPwm2Regs.TBCTR = 0; // clear TB counter
    EPwm2Regs.TBCTL.bit.CTRMODE = TB_COUNT_UPDOWN;
    EPwm2Regs.TBCTL.bit.PHSEN = TB_DISABLE; // Phase loading disabled
    EPwm2Regs.TBCTL.bit.PRDLD = TB_SHADOW;
    EPwm2Regs.TBCTL.bit.SYNCOSEL = TB_SYNC_DISABLE;
    EPwm2Regs.TBCTL.bit.HSPCLKDIV = TB_DIV1; // TBCLK = SYSCLKOUT
    EPwm2Regs.TBCTL.bit.CLKDIV = TB_DIV1;
    EPwm2Regs.CMPCTL.bit.SHDWAMODE = CC_SHADOW;
    EPwm2Regs.CMPCTL.bit.SHDWBMODE = CC_SHADOW;
    EPwm2Regs.CMPCTL.bit.LOADAMODE = CC_CTR_ZERO; // load on TBCTR = Zero
    EPwm2Regs.CMPCTL.bit.LOADBMODE = CC_CTR_ZERO; // load on TBCTR = Zero
    EPwm2Regs.AQCTLA.bit.CAU = AQ_SET;
    EPwm2Regs.AQCTLA.bit.CAD = AQ_CLEAR;
    EPwm2Regs.AQCTLB.bit.CBU = AQ_CLEAR;
    EPwm2Regs.AQCTLB.bit.CBD = AQ_SET;
    //
    EDIS;
}
//===============================================//
void Reset_Device(void)
{
    DINT;
    EALLOW;
    SysCtrlRegs.WDCR = 0x0047; // a wrong value in order to reset device
    EDIS;
}
//=============================================//
#pragma CODE_SECTION(cpu_timer0_isr,"ramfuncs");
__interrupt void cpu_timer0_isr(void)
{
    Update_PWM_Freq(Bridge_pwr_period, PwmGap);
    ADC_Bridge_Power();
    ADC_Avrager(0);
    DELAY_US(100);
    Bridge_En_Clr = 1;
    DELAY_US(2000);
    Bridge_En_Set = 1;
    Disable_PWM_Signal();
    DELAY_US(100);
    ADC_Bridge_Power();
    ADC_Avrager(1);
    ServiceDog();    // Reset the watchdog counter
    // stop timer0 , no further count
    //CpuTimer0Regs.TCR.bit.TIE = 0;
    //CpuTimer0Regs.TCR.bit.TSS = 1;
    //
    CpuTimer0Regs.TCR.bit.TIF = 1;
    CpuTimer0.InterruptCount++;
    // Acknowledge this interrupt to receive more interrupts from group 1
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP1;
}
//============================================//
#pragma CODE_SECTION(Update_PWM_Freq, "ramfuncs");
void Update_PWM_Freq(Uint16 Period, Uint16 Gap)
{
    // Period = 30000/PWM_Freq
    Uint16 CompA, CompB;
    CompA = Period + Gap;
    CompA = (CompA >> 1);
    CompB = Period - Gap;
    CompB = (CompB >> 1);
    EALLOW;
    EPwm2Regs.TBPRD = Period;  // Period = * TBCLK counts
    EPwm2Regs.CMPA.half.CMPA = CompA; // Compare A TBCLK counts
    EPwm2Regs.CMPB = CompB; // Compare B TBCLK counts
    //EPwm2Regs.TBCTR = 0; // clear TB counter
    PWM_Sig_1_Mux = 1;   // Configure as EPWM2A
    PWM_Sig_2_Mux = 1;   // Configure as EPWM2A
    //
    EDIS;
}
//===============================================//
#pragma CODE_SECTION(Disable_PWM_Signal, "ramfuncs");
void Disable_PWM_Signal(void)
{
    EALLOW;
    PWM_Sig_1_Mux = 0;
    PWM_Sig_1_Dir = 1;
    PWM_Sig_1_Clr = 1;
    PWM_Sig_2_Mux = 0;
    PWM_Sig_2_Dir = 1;
    PWM_Sig_2_Clr = 1;
    EDIS;
}
//===============================================//
#pragma CODE_SECTION(ADC_Bridge_Power, "ramfuncs");
void ADC_Bridge_Power(void)
{
    AdcRegs.ADCINTFLGCLR.bit.ADCINT3 = 1;
    AdcRegs.ADCSOCFRC1.bit.SOC2 = 1;
    while (AdcRegs.ADCINTFLG.bit.ADCINT3 == 0){}
    AdcRegs.ADCINTFLGCLR.bit.ADCINT3 = 1;
    Bridge_pwr_instant = AdcResult.ADCRESULT2;
}
//===============================================//
#pragma CODE_SECTION(ADC_Avrager, "ramfuncs");
void ADC_Avrager(Uint16 state)
{
    Uint16 k, temp1;
    if (state == 0)
    {
        Avg_counter++;
        if (Avg_counter >= 4)
        {
            Avg_counter = 0;
        }
        ADC_Bridge_zero_offset[Avg_counter] = Bridge_pwr_instant;
        temp1 = 0;
        for (k = 0; k < 4; k++)
        {
            temp1 += ADC_Bridge_zero_offset[k];
        }
        temp1 = (temp1 >> 2);
        Pwr_offset = temp1;
    }
    else
    {
        if (Bridge_pwr_instant < Pwr_offset)
        {
            ADC_BridgePwr_Buffer[Avg_counter] = Pwr_offset - Bridge_pwr_instant;
        }
        else
        {
            ADC_BridgePwr_Buffer[Avg_counter] = 0;
        }
        temp1 = 0;
        for (k = 0; k < 4; k++)
        {
            temp1 += ADC_BridgePwr_Buffer[k];
        }
        Bridge_pwr_Value = (temp1 >> 2);
    }
}
//=====================================//
#pragma CODE_SECTION(AGC_Bridge_Power, "ramfuncs");
void AGC_Bridge_Power(void)
{
    if (Bridge_pwr_Value > Pwm_Pwr_AGC_Threshold)
    {
        PwmGap++;
    }
    else
    {
        PwmGap--;
    }
    if (PwmGap > Max_Gap)
    {
        PwmGap = Max_Gap; // Min Gain
    }
    if (PwmGap < Min_Gap)
    {
        PwmGap = Min_Gap;    // Max Gain
    }
    if (Bridge_pwr_instant < short_circuit_threshold)   // short-circuit condition
    {
        PwmGap = Max_Gap;
        Reset_Device();
    }
}
//============================================//

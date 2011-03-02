/* FreeEMS - the open source engine management system
 *
 * Copyright 2008, 2009, 2010 Fred Cooke
 *
 * This file is part of the FreeEMS project.
 *
 * FreeEMS software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FreeEMS software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with any FreeEMS software.  If not, see http://www.gnu.org/licenses/
 *
 * We ask that if you make any changes to this file you email them upstream to
 * us at admin(at)diyefi(dot)org or, even better, fork the code on github.com!
 *
 * Thank you for choosing FreeEMS to run your engine!
 */


/**	@file fuelAndIgnitionCalcs.c
 * @ingroup measurementsAndCalculations
 *
 * @brief Fuel and ignition calculations.
 *
 * This file contains all of the main fuel and ignition calculations based
 * upon the variables that we have already determined in previous stages.
 *
 * @author Fred Cooke
 */


#define FUELANDIGNITIONCALCS_C
#include "inc/freeEMS.h"
#include "inc/utils.h"
#include "inc/commsCore.h"
#include "inc/tableLookup.h"
#include "inc/decoderInterface.h"
#include "inc/fuelAndIgnitionCalcs.h"


/** @brief Fuel and ignition calculations
 *
 * Using a variety of primary algorithms calculate a base pulsewidth and then
 * apply various corrections to it such as injector dead time, transient fuel
 * correction, engine temperature enrichment and per cylinder trims. The fuel
 * injection timing is also determined here.
 *
 * Calculate the ignition timing and dwell here too. Several corrections are
 * applied to these as well.
 *
 * @todo TODO implement the all of the ignition stuff and finish off all of the fuel injection stuff.
 * @todo TODO change the way configuration is done and make sure the most common options are after the first if().
 * @todo TODO add actual configuration options to the fixed config blocks for these items.
 *
 * @author Fred Cooke
 */
void calculateFuelAndIgnition(){
	/*&&&&&&&&&&&&& Perform the basic calculations one step at a time to get a final pulsewidth &&&&&&&&&&&&*/

	if(TRUE /* Genuine method */){
		unsigned short airInletTemp = CoreVars->IAT; /* All except MAF use this. */
		/* Determine the type of air flow data */
		if(TRUE /* SpeedDensity */){
			/* This won't overflow until 512kPa or about 60psi of boost with 128% VE. */
			DerivedVars->AirFlow = ((unsigned long)CoreVars->MAP * DerivedVars->VEMain) / oneHundredPercentVE;
			/* Result is 450 - 65535 always. */
		}else if(FALSE /*AlphaN*/){
			DerivedVars->AirFlow = DerivedVars->VEMain; /* Not actually VE, but rather tuned air flow without density information */
		}else if(FALSE /*MAF*/){
			DerivedVars->AirFlow = CoreVars->MAF; /* Just fix temperature at appropriate level to provide correct Lambda */
			/// @todo TODO figure out what the correct "temperature" is to make MAF work correctly!
			airInletTemp = roomTemperature; // 293.15k is 20c * 100 to get value, so divide by 100 to get real number
		}else if(FALSE /*FixedAF*/){ /* Fixed air flow from config */
			DerivedVars->AirFlow = fixedConfigs2.sensorPresets.presetAF;
		}else{ /* Default to no fuel delivery and error */
			DerivedVars->AirFlow = 0;
			/* If anyone is listening, let them know something is wrong */
//			sendError(AIRFLOW_NOT_CONFIGURED_CODE); // or maybe queue it?
		}


		/* This won't overflow until well past 125C inlet, 1.5 Lambda and fuel as dense as water */
		DerivedVars->densityAndFuel = (((unsigned long)((unsigned long)airInletTemp * DerivedVars->Lambda) / stoichiometricLambda) * fixedConfigs1.engineSettings.densityOfFuelAtSTP) / densityOfFuelTotalDivisor;
		/* Result is 7500 - 60000 always. */

		/* Divisors for air inlet temp and pressure :
		 * #define airInletTempDivisor 100
		 * #define airPressureDivisor 100
		 * cancel each other out! all others are used. */


		DerivedVars->BasePW = (bootFuelConst * DerivedVars->AirFlow) / DerivedVars->densityAndFuel;
	}else if(FALSE /*configured*/){ /* Fixed PW from config */
		DerivedVars->BasePW = fixedConfigs2.sensorPresets.presetBPW;
	}else{ /* Default to no fuel delivery and error */
		DerivedVars->BasePW = 0;
		/* If anyone is listening, let them know something is wrong */
//		sendError(BPW_NOT_CONFIGURED_CODE); // or maybe queue it?
	}

	/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/




	/*&&&&&&&&&&&&&&&&&&&&&&&&&&&& Apply All Corrections PCFC, ETE, IDT, TFC etc &&&&&&&&&&&&&&&&&&&&&&&&&&&*/

	/* Apply the corrections after calculating */
	DerivedVars->EffectivePW = safeTrim(DerivedVars->BasePW, DerivedVars->TFCTotal);
	DerivedVars->EffectivePW = safeScale(DerivedVars->EffectivePW, DerivedVars->ETE);


	unsigned char channel; // the declaration of this variable is used in multiple loops below.

	/* "Calculate" the individual fuel pulse widths */
	for(channel = 0; channel < INJECTION_CHANNELS; channel++){ /// @todo TODO make injector channels come from config, not defines.
		/* Add or subtract the per cylinder fuel trims */
		unsigned short channelPW;
		channelPW = safeScale(DerivedVars->EffectivePW, TablesB.SmallTablesB.perCylinderFuelTrims[channel]);

		/* Add on the IDT to get the final value and put it into the array */
		injectorMainPulseWidthsMath[channel] = safeAdd(channelPW, DerivedVars->IDT);
	}

	/* Reference PW for comparisons etc */
	unsigned short refPW = safeAdd(DerivedVars->EffectivePW, DerivedVars->IDT);
	DerivedVars->RefPW = refPW;
	/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/





/// TODO @todo FXIME part of to schedule or not to schedule should be : (masterPulseWidth > injectorMinimumPulseWidth)

// IE, NOT in the decoders... KISS in the decoders. This is a hangover from (very) early decoder dev

	// schedule even when RPM = zero and not synced, as sync could start any time and we want it to start injection/igniting then, up to decoder to not allow scheduling if not synced...
	// do not schedule, or schedule specially if rpm > max or hysteresis not met etc.

	/*&&&&&&&&&&&&&&&&& Based on IDT schedule PW start such that Fuel is correctly timed &&&&&&&&&&&&&&&&&&&*/

//	for(channel = 0;channel < INJECTION_CHANNELS;channel++){ /// @todo TODO make injector channels come from config, not defines.
		//injectorMainAdvances[channel] = IDT blah blah.
//	}

	/* This will involve using RPM, injector firing angle and IDT to schedule the events correctly */

	/** @todo TODO work needs to be done on scheduling before this can be completed. */

	/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/




	/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&& Calculate Dwell and Ignition angle &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
	/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/




	/*&&&&&&&&&&&&&&& Based on Dwell and Ignition angle schedule the start and end of dwell &&&&&&&&&&&&&&&&*/
	/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/




	/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&& TEMPORARY (and old) &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/

	/* "Calculate" the nominal total pulse width before per channel corrections */
	masterPulseWidth = refPW;








#define Mitsi4and1OffsetOnTruck 90 // add this to code degrees to find 0/TDC for cyl/output 1

//	unsigned short decoderEngineOffset = Mitsi4and1OffsetOnTruck;

// TDC output 1 and cylinder 1 is 0
// TDC output 2 and cylinder 3 is therefore 180
// TDC output 3 and cylinder 4 is therefore 360
// TDC output 4 and cylinder 2 is therefore 540

#define numberOfIgnitionEvents 4

	unsigned short anglesOfTDC[numberOfIgnitionEvents]; // no timing for fuel channels yet KISS for now.

	anglesOfTDC[0] = 0;
	anglesOfTDC[1] = 180;
	anglesOfTDC[2] = 360;
	anglesOfTDC[3] = 540;

	unsigned char ignitionEvent;
	for(ignitionEvent = 0;ignitionEvent < numberOfIgnitionEvents;ignitionEvent++){

		/* pseudo code
		 *
		 * we have:
		 *
		 * - offset between engine and code
		 * - offset for each output event TDC
		 * - desired timing value in degrees BTDC
		 * - a minimum post tooth delay
		 * - angle to ticks conversion number
		 *
		 * we want:
		 *
		 * - which event to fire from
		 * - how much to wait after that event before firing
		 *
		 * we need to:
		 *
		 * - to find the code angle that the spark must jump at
		 * - find nearest event
		 * - find time after nearest event to spark needing to jump
		 * - check that dwell + min delay < time after nearest
		 * - if so, set event number in output as nearest
		 * - and, set after delay to (distance between - dwell)
		 * - if not, set event number in output to one before nearest
		 * - and, set after delay to same + expected delay between nearest and next
		 *
		 * repeat per pin (this is in a loop)
		 */

		// eventAngles[?] ?

		// set the post event delay
		postReferenceEventDelays[ignitionEvent] = 0; // right after tooth till code complete

		// set the event to sched from
		pinEventNumbers[ignitionEvent] = 0xFF; // all off till code complete

	}









	// OLD shit :

//	postReferenceEventDelays[0] = 13000;
//	postReferenceEventDelays[1] = 13000;
//	postReferenceEventDelays[2] = 13000;
//	postReferenceEventDelays[3] = 13000;

	// from leading edge of slots, this produced TDC timing with 13000 post event delay and huge 36250 dwell
//	pinEventNumbers[0] = 7; // 1
//	pinEventNumbers[1] = 0; // 3
//	pinEventNumbers[2] = 2; // 4
//	pinEventNumbers[3] = 4; // 2

	// fuel shit:

	// just fire the fuel off whenever... doesn't matter much.
	postReferenceEventDelays[4] = 0;
	postReferenceEventDelays[5] = 0;

	// from alternate teeth so as to keep code simple for now.
	pinEventNumbers[4] = 1;
	pinEventNumbers[5] = 5;

	// nothing much, L&P:









	/* "Calculate" the individual fuel pulse widths */
	injectorMainPulseWidthsMath[0] = DesiredDwell;
	injectorMainPulseWidthsMath[1] = DesiredDwell;
	injectorMainPulseWidthsMath[2] = DesiredDwell;
	injectorMainPulseWidthsMath[3] = DesiredDwell;

	injectorMainPulseWidthsMath[4] = masterPulseWidth;
	injectorMainPulseWidthsMath[5] = masterPulseWidth;

	/// @todo TODO x 6 main pulsewidths, x 6 staged pulsewidths, x 6 flags for staged channels if(coreSettingsA & STAGED_ON){}

	/* Set the staged status on or off (for now based on changeable settings) */
	if(fixedConfigs1.coreSettingsA & STAGED_ON){
		coreStatusA |= STAGED_REQUIRED;
		/// @todo TODO determine the requirement for staged based on some sort of map and or complex load based configuration.
	}else{
		coreStatusA &= STAGED_NOT_REQUIRED;
	}

	// temporary ign tests
	unsigned short intendedAdvance = ADCArrays->MAT << 6;
	unsigned short intendedDwell = intendedAdvance >> 1;

	short c;
	for(c=0;c<IGNITION_CHANNELS;c++){
		ignitionAdvances[IGNITION_CHANNELS] = intendedAdvance;
	}
	*currentDwellMath = intendedDwell;

//	unsigned short minPeriod = ignitionMinimumDwell << 1;
	//	if(intendedDwell < ignitionMinimumDwell){
//		dwellLength = ignitionMinimumDwell;
//	}else{
//		dwellLength = intendedDwell;
//	}
//	if(intendedPeriod < minPeriod){
//		dwellPeriod = minPeriod;
//	}else{
//		dwellPeriod = intendedPeriod;
//	}
//	PITLD0 = dwellPeriod;

	/** @todo TODO Calculate the dwell period (one of) */

	/** @todo TODO Calculate the ignition advances (twelve of) */

	/*&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&& TEMPORARY END &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&*/
}

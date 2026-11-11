// DDD role: ValueObject

package schemas

// #Measurement is one spike result. Every threshold is a ratio against a
// same-machine baseline, never a wall-clock absolute (P-03).
#Measurement: {
    id:        #MeasurementId
    observed:  number & >=0
    baseline?: number & >=0
    verdict:   #Verdict
}

// #MeasurementId closes the set to the seven the spike runs.
#MeasurementId: "notifier-fires" | "paint-survives" | "modal-survives" | "nested-loops-deliver" | "deadline-drives-timer" | "turn-honours-deadline" | "appnap-survives"

// #Verdict is what a measurement concludes.
#Verdict: "pass" | "fail" | "inconclusive"

// #SpikeResult is the whole run.
#SpikeResult: {
    host:         string & !=""
    qtVersion:    string & !=""
    measurements: [...#Measurement]
    leaks:        #Verdict
}

import bb.cascades 1.0
import QtMobilitySubset.location 1.1
import bb.multimedia 1.0
import Network.PostHttp 1.0
import "controls"
import my.library 1.0

Page {
    Container {
        layout: DockLayout {
        }
        Label {
            id: msg
            text: "code"
            verticalAlignment: VerticalAlignment.Bottom
            horizontalAlignment: HorizontalAlignment.Center
        }
        Button {
            id:btn1
            text: "Record"
            onClicked: {
                btn1.text = "Recording...";
                recorder.record();
            }
            horizontalAlignment: HorizontalAlignment.Center
        }
          Button {
                    id:btn2
                    text:"Stop"
                    
                     onClicked: {
                         recorder.reset();
                         btn1.text = "Record"
                          wavReSample.wav_resample();
                          netpost2.dir_post(new String("heeelaa"));
                          
                                }
            verticalAlignment: VerticalAlignment.Center
            horizontalAlignment: HorizontalAlignment.Center
        }
                
       attachedObjects: [
           AudioRecorder {
               id: recorder
               outputUrl: "file:///accounts/1000/shared/misc/vloc.wav"
           },
                               PostHttp {
                                   id: wavReSample
                                   onComplete: {
                                   }
                               },
                               PostHttp {
                                   id: netpost2
                                   onComplete: {
                                        msg.text = info;
                                   }
                               } 
       ] 
    }
}

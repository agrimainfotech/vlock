// Default empty project template
import bb.cascades 1.0
import QtMobilitySubset.location 1.1
import bb.multimedia 1.0
import Network.PostHttp 1.0
import "controls"
import my.library 1.0

// creates one page with a label

NavigationPane {
    property string latitude
    property string longitude
    property variant currentCoord
    property string imei
    property bool flag: false
    property bool flag2: true
    property string link
    id: navPane
    backButtonsVisible: false
   
        Page {
            id: mainPage
            actionBarVisibility: ChromeVisibility.Hidden
            Container {
                id: root
                property int currentCount: 0
                layout: DockLayout {
            }
            objectName: "main 2 final.psd"
                preferredWidth: 768
                preferredHeight: 1280
               
                 Button {
                                id:btn1
                                onClicked: {
                                  btn1.text="Recording...";  
                                  btn1.record();
                                }
                                text: "Record"
                                verticalAlignment: VerticalAlignment.Top
                                horizontalAlignment: HorizontalAlignment.Center
                                layoutProperties: AbsoluteLayoutProperties {
                                    positionX: 2.3
                                    positionY: 5.0
                                }
                            }
                            Button {
                                id:btn2
                             onClicked: {
                                 btn1.text = "Record";
                                 recorder.reset();
                                  
                                                
                                            }
                                            text: "Stop"
                                            verticalAlignment: VerticalAlignment.Center
                                            horizontalAlignment: HorizontalAlignment.Center
                                           
                            }
            
 
        
                attachedObjects: [
                    QTimer {
                        id: recordTimer
                        interval: 1000
                        onTimeout: {
                            root.currentCount -= 1;
                            if (root.currentCount == 0) {
                                recordButton.enabled = false;
                                recorder.reset();
                                recordTimer.stop();
                                // recordButton.setDisabledImageSource("asset:///images/small.gif");
                                wavReSample.wav_resample();
                                //netpost.post();
                            //    if (link != "not confirmed") {
                                    netpost2.dir_post(new String(link));
                             //   } else {
                              //      webView.url = "www.google.com";
                             //   }
                                flag = false;
                            }
                        }
                    },
                    AudioRecorder {
                        id: recorder
                        outputUrl: "/accounts/1000/shared/voice/vLock.wav"
                    },
                    QTimer {
                        id: timer2
                        interval: 200
                        onTimeout: {
                        }
                    },
                    PostHttp {
                        id: wavReSample
                        onComplete: {
                        }
                    },
                    PostHttp {
                        id: netpost2
                        onComplete: {
                             
                        }
                    } 
                ]
           
        }
            attachedObjects: [
                MediaPlayer {
                    id: myPlayer
                }
            ]
}
}
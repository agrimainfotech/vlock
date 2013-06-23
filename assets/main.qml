// Default empty project template
import bb.cascades 1.0
import QtMobilitySubset.location 1.1
import bb.multimedia 1.0
import Network.PostHttp 1.0
import "controls"
import my.library 1.0

// creates one page with a label

Page {
    Container {
        id: mainContainer
        property int count: 0
        layout: AbsoluteLayout {
        }
        objectName: "numberttons"
        preferredWidth: 768
        preferredHeight: 1280
        background: Color.create("#000000")
        Container {
            id: upper
            layout: AbsoluteLayout {
            }
            Container {
                layout: AbsoluteLayout {
                }
                objectName: "numberttons"
                preferredWidth: 768
                preferredHeight: 1280

                ///////////////

                /////////////////
                Label {
                    id: securityCode
                    objectName: "SECURITY CODE"
                    text: "Enter your key"
                    textStyle.color: Color.create("#2F8DC8")
                    textStyle.fontSize: FontSize.PointValue
                    textStyle.fontSizeValue: 16.3
                    textStyle.fontFamily: "Myriad Pro"
                    textStyle.fontStyle: FontStyle.Normal
                    layoutProperties: AbsoluteLayoutProperties {
                        positionX: 110
                        positionY: 203
                    }
                    textStyle.textAlign: TextAlign.Center
                }
                Label {
                    id: verificationCode
                    objectName: "Verification CODE"
                    text: "Access Command"
                    textStyle.color: Color.create("#2F8DC8")
                    textStyle.fontSize: FontSize.PointValue
                    textStyle.fontSizeValue: 16.3
                    textStyle.fontFamily: "Myriad Pro"
                    textStyle.fontStyle: FontStyle.Normal
                    layoutProperties: AbsoluteLayoutProperties {
                        positionX: 100
                        positionY: 100
                    }
                    textStyle.textAlign: TextAlign.Center
                }
            }
        }
        Container {
            id: number
            layout: AbsoluteLayout {
            }
            visible: false
            ImageButton {
                objectName: "down"
                defaultImageSource: "asset:///images/down.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 280
                    positionY: 372
                }
                preferredWidth: 212
                preferredHeight: 142
                onClicked: {
                    lower.visible = true;
                    uptodwn.play();
                    lowerToUpper.play();
                    number.visible = false;
                }
            }
            ImageButton {
                objectName: "X "
                defaultImageSource: "asset:///images/X_.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 21
                    positionY: 1065
                }
                preferredWidth: 254
                preferredHeight: 211
                onClicked: {
                    securityCode.text = "";
                    mainContainer.count = 0;
                }
            }
            ImageButton {
                objectName: "0"
                defaultImageSource: "asset:///images/0.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 261
                    positionY: 1065
                }
                preferredWidth: 254
                preferredHeight: 211
                onClicked: {
                    securityCod("0");
                }
            }
            ImageButton {
                objectName: "Shape 1"
                defaultImageSource: "asset:///images/Shape_1.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 503
                    positionY: 1065
                }
                preferredWidth: 254
                preferredHeight: 211
                onClicked: {
                }
            }
            ImageButton {
                objectName: "7"
                defaultImageSource: "asset:///images/7.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 21
                    positionY: 876
                }
                preferredWidth: 254
                preferredHeight: 211
                onClicked: {
                    securityCod("7");
                }
            }
            ImageButton {
                objectName: "8"
                defaultImageSource: "asset:///images/8.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 261
                    positionY: 876
                }
                preferredWidth: 254
                preferredHeight: 211
                onClicked: {
                    securityCod("8");
                }
            }
            ImageButton {
                objectName: "9"
                defaultImageSource: "asset:///images/9.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 503
                    positionY: 876
                }
                preferredWidth: 254
                preferredHeight: 211
                onClicked: {
                    securityCod("9");
                }
            }
            ImageButton {
                objectName: "6"
                defaultImageSource: "asset:///images/6.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 503
                    positionY: 692
                }
                preferredWidth: 254
                preferredHeight: 210
                onClicked: {
                    securityCod("6");
                }
            }
            ImageButton {
                objectName: "5"
                defaultImageSource: "asset:///images/5.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 261
                    positionY: 692
                }
                preferredWidth: 254
                preferredHeight: 210
                onClicked: {
                    securityCod("5");
                }
            }
            ImageButton {
                objectName: "4"
                defaultImageSource: "asset:///images/4.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 21
                    positionY: 692
                }
                preferredWidth: 254
                preferredHeight: 210
                onClicked: {
                    securityCod("4");
                }
            }
            ImageButton {
                objectName: "1"
                defaultImageSource: "asset:///images/1.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 21
                    positionY: 499
                }
                preferredWidth: 254
                preferredHeight: 211
                onClicked: {
                    securityCod("1");
                }
            }
            ImageButton {
                objectName: "2"
                defaultImageSource: "asset:///images/2.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 261
                    positionY: 499
                }
                preferredWidth: 254
                preferredHeight: 211
                onClicked: {
                    securityCod("2");
                }
            }
            ImageButton {
                objectName: "3"
                defaultImageSource: "asset:///images/3.png"
                layoutProperties: AbsoluteLayoutProperties {
                    positionX: 503
                    positionY: 499
                }
                preferredWidth: 254
                preferredHeight: 211
                onClicked: {
                    securityCod("3");
                }
            }
            animations: [
                TranslateTransition {
                    id: upAnimation
                    fromY: 1280
                    toY: 0
                    duration: 800
                },
                TranslateTransition {
                    id: uptodwn
                    toY: 1280
                    duration: 800
                }
            ]
        }
        Container {
            id: lower
            layout: AbsoluteLayout {
            }
            Container {
                layout: AbsoluteLayout {
                }
                objectName: "numberttons"
                preferredWidth: 768
                preferredHeight: 1280
                ImageButton {
                    objectName: "SpeakBtn - ImageButton Control"
                    defaultImageSource: "asset:///images/SpeakBtn_-_ImageButton_Control.png"
                    layoutProperties: AbsoluteLayoutProperties {
                        positionX: 299
                        positionY: 1103
                    }
                    preferredWidth: 168
                    preferredHeight: 168
                    onTouchEnter: {
                        myPlayer.setSourceUrl("asset:///button.wav")
                        myPlayer.play();
                        recorder.record();
                    }
                    /*
                     * onClicked: {
                     * 
                     * myPlayer.setSourceUrl("asset:///button.wav")
                     * myPlayer.play();
                     * recorder.record();
                     * }
                     */
                    onTouchExit: {
                        recorder.reset();
                        wavReSample.wav_resample();
                        netpost2.dir_post(new String("heeelaa"));
                    }
                }
                ImageButton {
                    objectName: "UpBtn - ImageButton Control"
                    defaultImageSource: "asset:///images/UpBtn_-_ImageButton_Control.png"
                    layoutProperties: AbsoluteLayoutProperties {
                        positionX: 277
                        positionY: 962
                    }
                    preferredWidth: 212
                    preferredHeight: 142
                    onClicked: {
                        number.visible = true
                        lowerAnimation.play()
                        upAnimation.play()
                        lower.visible = false
                    }
                }
                animations: [
                    TranslateTransition {
                        id: lowerAnimation
                        toY: 1900
                        duration: 500
                    },
                    TranslateTransition {
                        id: lowerToUpper
                        toY: 0
                        duration: 500
                    }
                ]
            }
        }
        attachedObjects: [
            MediaPlayer {
                id: myPlayer
            },
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
                    securityCode.text = info;
                }
            }
        ]
    }
    function securityCod(value) {
        myPlayer.setSourceUrl("asset:///button.wav")
        myPlayer.play()
        if (securityCode.text == "Enter your key") {
            securityCode.text = "";
        }
        if (mainContainer.count <= 4) {
            securityCode.text += value;
            mainContainer.count ++;
        } else {
            securityCode.text = "";
            mainContainer.count = 0;
        }
    }
}

import { HttpErrorResponse } from '@angular/common/http';
import { Component, Input, OnInit } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';

@Component({
  selector: 'app-autospeed',
  templateUrl: './autospeed.component.html',
  styleUrls: ['./autospeed.component.scss']
})
export class AutospeedComponent implements OnInit {
  public form!: FormGroup;
  public savedChanges: boolean = false;

  @Input() uri = '';

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private loadingService: LoadingService
  ) {}

  ngOnInit(): void {
    this.systemService.getInfo(this.uri)
      .pipe(
        this.loadingService.lockUIUntilComplete()
      )
      .subscribe(info => {
        this.form = this.fb.group({
          powerlow: [info.powerlow, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(0),
            Validators.max(50)
          ]],
          powerhigh: [info.powerhigh, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(0),
            Validators.max(50)
          ]],
          asicvoltlow: [info.asicvoltlow, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(1000),
            Validators.max(1500)
          ]],
          asicvolthigh: [info.asicvolthigh, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(1000),
            Validators.max(1500)
          ]],
          asictemplow: [info.asictemplow, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(30),
            Validators.max(90)
          ]],
          asictemphigh: [info.asictemphigh, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(30),
            Validators.max(90)
          ]],
          vrtemplow: [info.vrtemplow, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(30),
            Validators.max(120)
          ]],
          vrtemphigh: [info.vrtemphigh, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(30),
            Validators.max(120)
          ]],
          hashlow: [info.hashlow, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(50),
            Validators.max(1000)
          ]],
          hashhigh: [info.hashhigh, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(50),
            Validators.max(1000)
          ]],
          fantarget: [info.fantarget, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(50),
            Validators.max(95)
          ]],
          autospeed: [info.autospeed == 1, [Validators.required]]
        });
      });
  }

  public updateSystem() {
    const form = this.form.getRawValue();
    form.powerlow = parseInt(form.powerlow);
    form.powerhigh = parseInt(form.powerhigh);
    form.asicvoltlow = parseInt(form.asicvoltlow);
    form.asicvolthigh = parseInt(form.asicvolthigh);
    form.asictemplow = parseInt(form.asictemplow);
    form.asictemphigh = parseInt(form.asictemphigh);
    form.vrtemplow = parseInt(form.vrtemplow);
    form.vrtemphigh = parseInt(form.vrtemphigh);
    form.hashlow = parseInt(form.hashlow);
    form.hashhigh = parseInt(form.hashhigh);
    form.fantarget = parseInt(form.fantarget);
    // bools to ints
    form.autospeed = form.autospeed == true ? 1 : 0;
   
    this.systemService.updateSystem(this.uri, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Saved auto speed settings for ${this.uri}` : 'Saved auto speed settings';
          this.toastr.success(successMessage, 'Success!');
          this.savedChanges = true;
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Could not save auto speed settings for ${this.uri}. ${err.message}` : `Could not save auto speed settings. ${err.message}`;
          this.toastr.error(errorMessage, 'Error');
          this.savedChanges = false;
        }
      });
  }

  showStratumPassword: boolean = false;
  toggleStratumPasswordVisibility() {
    this.showStratumPassword = !this.showStratumPassword;
  }

  showFallbackStratumPassword: boolean = false;
  toggleFallbackStratumPasswordVisibility() {
    this.showFallbackStratumPassword = !this.showFallbackStratumPassword;
  }

  public restart() {
    this.systemService.restart(this.uri)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          const successMessage = this.uri ? `Bitaxe at ${this.uri} restarted` : 'Bitaxe restarted';
          this.toastr.success(successMessage, 'Success');
        },
        error: (err: HttpErrorResponse) => {
          const errorMessage = this.uri ? `Failed to restart device at ${this.uri}. ${err.message}` : `Failed to restart device. ${err.message}`;
          this.toastr.error(errorMessage, 'Error');
        }
      });
  }
}

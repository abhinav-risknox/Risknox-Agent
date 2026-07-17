using System;
using System.DirectoryServices.AccountManagement;
using System.Windows;
using System.Windows.Input;

namespace RisknoxMonitor
{
    public partial class PasswordDialog : Window
    {
        public PasswordDialog()
        {
            InitializeComponent();
            InstructionText.Text = $"Please enter the password for {Environment.UserDomainName}\\{Environment.UserName} to verify administrator privileges.";
            Loaded += (s, e) =>
            {
                PwdBox.Focus();
                Keyboard.Focus(PwdBox);
            };
        }

        private async void BtnOk_Click(object sender, RoutedEventArgs e)
        {
            ErrorText.Visibility = Visibility.Collapsed;
            string password = PwdBox.Password;
            string username = Environment.UserName;
            string domain = Environment.UserDomainName;

            if (string.IsNullOrWhiteSpace(password))
            {
                ErrorText.Text = "Please enter a password.";
                ErrorText.Visibility = Visibility.Visible;
                return;
            }

            BtnOk.IsEnabled = false;
            BtnCancel.IsEnabled = false;
            BtnOk.Content = "Authenticating...";
            PwdBox.IsEnabled = false;

            bool isLocal = Environment.MachineName.Equals(domain, StringComparison.OrdinalIgnoreCase);
            ContextType ctxType = isLocal ? ContextType.Machine : ContextType.Domain;

            try
            {
                var result = await System.Threading.Tasks.Task.Run(() =>
                {
                    using (PrincipalContext context = new PrincipalContext(ctxType, domain))
                    {
                        if (context.ValidateCredentials(username, password))
                        {
                            bool isAdmin = false;
                            using (UserPrincipal user = UserPrincipal.FindByIdentity(context, IdentityType.SamAccountName, username))
                            {
                                if (user != null)
                                {
                                    using (PrincipalContext localContext = new PrincipalContext(ContextType.Machine))
                                    {
                                        using (GroupPrincipal adminGroup = GroupPrincipal.FindByIdentity(localContext, IdentityType.Sid, "S-1-5-32-544"))
                                        {
                                            if (adminGroup != null && user.IsMemberOf(adminGroup))
                                            {
                                                isAdmin = true;
                                            }
                                        }
                                    }
                                }
                            }
                            return isAdmin ? "SUCCESS" : "NO_ADMIN";
                        }
                        return "INVALID_PASSWORD";
                    }
                });

                if (result == "SUCCESS")
                {
                    DialogResult = true;
                }
                else if (result == "NO_ADMIN")
                {
                    ErrorText.Text = "This account does not have Administrator privileges.";
                    ErrorText.Visibility = Visibility.Visible;
                    PwdBox.Clear();
                }
                else
                {
                    ErrorText.Text = "Incorrect password.";
                    ErrorText.Visibility = Visibility.Visible;
                    PwdBox.Clear();
                }
            }
            catch (Exception ex)
            {
                ErrorText.Text = $"Authentication error: {ex.Message}";
                ErrorText.Visibility = Visibility.Visible;
                PwdBox.Clear();
            }
            finally
            {
                BtnOk.IsEnabled = true;
                BtnCancel.IsEnabled = true;
                BtnOk.Content = "Stop Agent";
                PwdBox.IsEnabled = true;
                if (ErrorText.Visibility == Visibility.Visible)
                {
                    PwdBox.Focus();
                }
            }
        }

        private void BtnCancel_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = false;
        }
    }
}
